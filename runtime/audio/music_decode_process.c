#include "music_decode.h"

#include <SDL3/SDL.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static int decoder_open(const char *path, int sample_rate, int channels, pid_t *output_pid)
{
    int descriptors[2];
    if (pipe(descriptors) != 0) return -1;

    char rate[16];
    char channel_count[8];
    snprintf(rate, sizeof rate, "%d", sample_rate);
    snprintf(channel_count, sizeof channel_count, "%d", channels);

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(descriptors[0]);
        close(descriptors[1]);
        return -1;
    }
    const int action_error = posix_spawn_file_actions_addclose(&actions, descriptors[0]) ||
                             posix_spawn_file_actions_adddup2(&actions, descriptors[1], STDOUT_FILENO) ||
                             posix_spawn_file_actions_addclose(&actions, descriptors[1]) ||
                             posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    if (action_error) {
        posix_spawn_file_actions_destroy(&actions);
        close(descriptors[0]);
        close(descriptors[1]);
        return -1;
    }

    char *const arguments[] = {"ffmpeg",  "-v",        "quiet", "-nostdin", "-i",  (char *)path,  "-f", "s16le",
                               "-acodec", "pcm_s16le", "-ar",   rate,       "-ac", channel_count, "-",  NULL};
    pid_t process = -1;
    const int spawn_error = posix_spawnp(&process, "ffmpeg", &actions, NULL, arguments, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(descriptors[1]);
    if (spawn_error) {
        close(descriptors[0]);
        errno = spawn_error;
        return -1;
    }
    *output_pid = process;
    return descriptors[0];
}

static void terminate_decoder(pid_t process)
{
    if (process <= 0) return;
    kill(process, SIGKILL);
    while (waitpid(process, NULL, 0) < 0 && errno == EINTR) {
    }
}

int music_decode_file(const char *path, int sample_rate, int channels, int16_t **samples, size_t *frames, char *error,
                      size_t error_capacity)
{
    if (!path || !samples || !frames || !error || error_capacity == 0 || sample_rate <= 0 || channels <= 0) return 0;
    *samples = NULL;
    *frames = 0;
    error[0] = 0;

    pid_t process = -1;
    const int descriptor = decoder_open(path, sample_rate, channels, &process);
    if (descriptor < 0) {
        snprintf(error, error_capacity, "could not start ffmpeg: %s", strerror(errno));
        return 0;
    }

    enum { DECODE_TIMEOUT_MS = 30000 };
    const Uint64 deadline = SDL_GetTicks() + DECODE_TIMEOUT_MS;
    const size_t maximum_frames = music_decode_frame_limit(sample_rate);
    if (maximum_frames == 0 || (size_t)channels > SIZE_MAX / sizeof(int16_t) / maximum_frames) {
        snprintf(error, error_capacity, "invalid decoded music size limit");
        terminate_decoder(process);
        close(descriptor);
        return 0;
    }
    const size_t maximum_bytes = maximum_frames * (size_t)channels * sizeof(int16_t);
    size_t capacity = maximum_bytes < (1u << 20) ? maximum_bytes : (1u << 20);
    size_t length = 0;
    int16_t *buffer = malloc(capacity);
    int failed = buffer == NULL;
    while (!failed) {
        if (length == capacity) {
            if (capacity == maximum_bytes) {
                snprintf(error, error_capacity, "decoded music exceeds the ten-minute safety limit");
                failed = 1;
                break;
            }
            const size_t grown = capacity > maximum_bytes / 2 ? maximum_bytes : capacity * 2;
            int16_t *larger = realloc(buffer, grown);
            if (!larger) {
                failed = 1;
                break;
            }
            buffer = larger;
            capacity = grown;
        }
        const Uint64 now = SDL_GetTicks();
        if (now >= deadline) {
            snprintf(error, error_capacity, "ffmpeg did not finish within %d ms for %s", DECODE_TIMEOUT_MS, path);
            failed = 1;
            break;
        }
        struct pollfd ready = {descriptor, POLLIN | POLLHUP, 0};
        const int wait_ms = (int)((deadline - now) < 250 ? deadline - now : 250);
        const int polled = poll(&ready, 1, wait_ms);
        if (polled < 0) {
            if (errno == EINTR) continue;
            failed = 1;
            break;
        }
        if (polled == 0) continue;
        const ssize_t count = read(descriptor, (char *)buffer + length, capacity - length);
        if (count > 0) {
            length += (size_t)count;
            continue;
        }
        if (count == 0) break;
        if (errno != EINTR && errno != EAGAIN) failed = 1;
    }
    close(descriptor);

    int status = 0;
    while (!failed) {
        const pid_t waited = waitpid(process, &status, WNOHANG);
        if (waited == process) break;
        if (waited < 0 && errno != EINTR) {
            failed = 1;
            process = -1;
            break;
        }
        if (SDL_GetTicks() >= deadline) {
            snprintf(error, error_capacity, "ffmpeg closed output but did not exit for %s", path);
            failed = 1;
            break;
        }
        SDL_Delay(5);
    }
    if (failed || length == 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        terminate_decoder(process);
        free(buffer);
        if (!error[0]) {
            const int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            snprintf(error, error_capacity, "no audio decoded from %s%s", path,
                     exit_code == 127 ? " (ffmpeg not found on PATH)" : "");
        }
        return 0;
    }

    const size_t frame_bytes = sizeof(int16_t) * (size_t)channels;
    if (length % frame_bytes != 0) {
        free(buffer);
        snprintf(error, error_capacity, "ffmpeg returned an incomplete PCM frame for %s", path);
        return 0;
    }
    *samples = buffer;
    *frames = length / frame_bytes;
    return *frames > 0;
}
