/* Native frame-data parser for fn_0040ef70.
 *
 * LF2's constructor mixes two very different jobs in one 13 KB x86 function: a small
 * resource/metadata prefix and a token-at-a-time parser for as many as 400 frame records.
 * The latter made 466,000 guest->host fscanf calls during startup. The override keeps the
 * original constructor as a super-call over the prefix, then writes the frame records from
 * the same plaintext with a native tokenizer. Bitmap, sound and weapon-strength setup stay
 * in LF2's code; only its hot text parser is replaced.
 */

#include "environment.h"
#include "object_parser.h"

#include "guest.h"
#include "jit_executor.h"
#include "paths.h"
#include "lf2_log.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    TEMP_PATH_GUEST = 0x00447b1c,
    CHECKSUM = 0x0044f620,
    OBJECT_BYTES = 0x25360,
    FRAME_COUNT = 400,
    FRAME_STRIDE = 0x178,
    FRAME_SOUND_PATH = 0x914,
    FRAME_ITR_COUNT = 0x8cc,
    FRAME_BDY_COUNT = 0x8d0,
    FRAME_ITR_POINTER = 0x8d4,
    FRAME_BDY_POINTER = 0x8d8,
};

int object_token_next(ObjectTokenStream *stream, char *out, size_t capacity)
{
    while (stream->cursor < stream->end && isspace((unsigned char)*stream->cursor)) ++stream->cursor;
    if (stream->cursor == stream->end) return 0;

    const char *begin = stream->cursor;
    while (stream->cursor < stream->end && !isspace((unsigned char)*stream->cursor)) ++stream->cursor;
    const size_t length = (size_t)(stream->cursor - begin);
    if (!capacity || length >= capacity) return -1;
    memcpy(out, begin, length);
    out[length] = 0;
    return 1;
}

int object_token_int(ObjectTokenStream *stream, int32_t *out)
{
    char token[64];
    if (object_token_next(stream, token, sizeof token) != 1) return 0;
    char *end = NULL;
    errno = 0;
    const long value = strtol(token, &end, 10);
    if (errno || end == token || *end || value < INT32_MIN || value > INT32_MAX) return 0;
    *out = (int32_t)value;
    return 1;
}

static const char *first_frame_token(const char *text, size_t size)
{
    ObjectTokenStream stream = {text, text + size};
    char token[256];
    for (;;) {
        const char *begin = stream.cursor;
        const int result = object_token_next(&stream, token, sizeof token);
        if (result <= 0) return NULL;
        if (strcmp(token, "<frame>") == 0) {
            while (begin < text + size && isspace((unsigned char)*begin)) ++begin;
            return begin;
        }
    }
}

static int write_prefix(const char *text, const char *frames)
{
    FILE *file = fopen(lf2_host_path("data\\temporary.txt"), "wb");
    if (!file) return 0;
    const size_t length = (size_t)(frames - text);
    const int ok = fwrite(text, 1, length, file) == length && fputc('\n', file) != EOF;
    return fclose(file) == 0 && ok;
}

static void call_decrypt(uint32_t path)
{
    PUSH32(path);
    PUSH32(0x0040ef70);
    lf2_jit_call(0x004148a0);
    R(ESP) += 4;
}

static void call_prefix_super(uint32_t object, uint32_t id, uint32_t type, uint32_t presenter)
{
    PUSH32(presenter);
    PUSH32(TEMP_PATH_GUEST);
    PUSH32(type);
    PUSH32(id);
    R(ECX) = object;
    PUSH32(0x0040ef71);
    lf2_jit_call_original(0x0040ef70);
}

void object_parser_checksum_token(const char *token)
{
    uint32_t checksum = LD32(CHECKSUM);
    for (size_t i = 0; token[i]; ++i) checksum += (uint32_t)(int32_t)(int8_t)token[i] * (uint32_t)i;
    ST32(CHECKSUM, checksum);
}

static uint32_t checksum_token_value(const char *token)
{
    uint32_t value = 0;
    for (size_t i = 0; token[i]; ++i) value += (uint32_t)(int32_t)(int8_t)token[i] * (uint32_t)i;
    return value;
}

static uint32_t last_token_value(const char *begin, const char *end)
{
    ObjectTokenStream stream = {begin, end};
    char token[256];
    uint32_t value = 0;
    while (object_token_next(&stream, token, sizeof token) == 1) value = checksum_token_value(token);
    return value;
}

/* The original loop checks feof() before fscanf(). At an EOF reached through trailing
 * whitespace, the failed scan leaves its token buffer unchanged and LF2 checksums the final
 * token once more. Our prefix super-call necessarily reaches that same path at the artificial
 * EOF, so replace its repeated prefix token with the full file's repeated final token. */
static void correct_prefix_eof_checksum(const char *text, const char *frames, size_t size)
{
    uint32_t checksum = LD32(CHECKSUM);
    checksum -= last_token_value(text, frames);
    if (size && isspace((unsigned char)text[size - 1])) checksum += last_token_value(frames, text + size);
    ST32(CHECKSUM, checksum);
}

static void dump_object(uint32_t object)
{
    const char *directory = lf2_environment_get(LF2_ENV_OBJECT_PARSER_DUMP);
    if (!directory || !*directory) return;
    static unsigned ordinal;
    char path[1024];
    snprintf(path, sizeof path, "%s/%03u.bin", directory, ordinal++);
    FILE *file = fopen(path, "wb");
    if (!file) {
        lf2_log_writef(LF2_LOG_INFO, "object-parser", "object parser: cannot write %s\n", path);
        return;
    }
    fwrite(g_mem + object, 1, OBJECT_BYTES, file);
    for (uint32_t i = 0; i < FRAME_COUNT; ++i) {
        const uint32_t frame = object + i * FRAME_STRIDE;
        const uint32_t sound = LD32(frame + FRAME_SOUND_PATH);
        if (sound) fwrite(g_mem + sound, 1, strlen((const char *)g_mem + sound) + 1, file);
        const uint32_t itr_count = LD32(frame + FRAME_ITR_COUNT);
        if (itr_count) fwrite(g_mem + LD32(frame + FRAME_ITR_POINTER), 80, itr_count, file);
        const uint32_t bdy_count = LD32(frame + FRAME_BDY_COUNT);
        if (bdy_count) fwrite(g_mem + LD32(frame + FRAME_BDY_POINTER), 40, bdy_count, file);
    }
    const uint32_t checksum = LD32(CHECKSUM);
    fwrite(&checksum, sizeof checksum, 1, file);
    fclose(file);
}

void fn_0040ef70(void)
{
    const uint32_t entry_esp = R(ESP);
    const uint32_t object = R(ECX);
    const uint32_t id = LD32(entry_esp + 4);
    const uint32_t type = LD32(entry_esp + 8);
    const uint32_t path = LD32(entry_esp + 12);
    const uint32_t presenter = LD32(entry_esp + 16);

    call_decrypt(path);
    size_t size = 0;
    char *text = lf2_read_text(lf2_host_path("data\\temporary.txt"), &size);
    const char *frames = text ? first_frame_token(text, size) : NULL;
    if (!text || !frames || !write_prefix(text, frames)) {
        free(text);
        R(ESP) = entry_esp;
        lf2_jit_call_original(0x0040ef70);
        dump_object(object);
        return;
    }

    call_prefix_super(object, id, type, presenter);
    correct_prefix_eof_checksum(text, frames, size);
    if (!object_parser_load_frames(object, frames, (size_t)((text + size) - frames))) {
        lf2_log_writef(LF2_LOG_INFO, "object-parser", "object parser: malformed frame data in %s\n", g_mem + path);
        abort();
    }
    free(text);
    dump_object(object);
    R(EAX) = object;
    R(ESP) = entry_esp + 20;
}
