#include "android_bridge.h"

#include <lucent/platform_c.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_system.h>

#include <jni.h>
#include <stdio.h>
#include <string.h>

typedef struct AndroidSelection {
    SDL_Mutex *mutex;
    SDL_Condition *condition;
    int waiting;
    int complete;
    int failed;
    char executable[4096];
    char error[512];
} AndroidSelection;

static AndroidSelection selection;

static int copy_text(char *output, size_t capacity, const char *value)
{
    if (!output || capacity == 0 || !value) return 0;
    const int written = snprintf(output, capacity, "%s", value);
    return written >= 0 && (size_t)written < capacity;
}

static int ensure_selection_state(void)
{
    if (selection.mutex && selection.condition) return 1;
    selection.mutex = SDL_CreateMutex();
    selection.condition = SDL_CreateCondition();
    if (selection.mutex && selection.condition) return 1;
    fprintf(stderr, "android: cannot create game-import synchronization state: %s\n", SDL_GetError());
    return 0;
}

static int call_activity_void_string(const char *method_name, const char *argument, char *error, size_t error_capacity)
{
    JNIEnv *environment = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jobject activity = (jobject)SDL_GetAndroidActivity();
    if (!environment || !activity) {
        snprintf(error, error_capacity, "Android Activity is not available: %s", SDL_GetError());
        return 0;
    }
    jclass activity_class = (*environment)->GetObjectClass(environment, activity);
    jmethodID method =
        activity_class ? (*environment)->GetMethodID(environment, activity_class, method_name, "(Ljava/lang/String;)V")
                       : NULL;
    jstring java_argument = argument ? (*environment)->NewStringUTF(environment, argument) : NULL;
    if (!activity_class || !method || (argument && !java_argument)) {
        snprintf(error, error_capacity, "Android Activity is missing %s(String)", method_name);
    } else {
        (*environment)->CallVoidMethod(environment, activity, method, java_argument);
        if ((*environment)->ExceptionCheck(environment)) {
            (*environment)->ExceptionClear(environment);
            snprintf(error, error_capacity, "Android Activity failed while calling %s", method_name);
        }
    }
    if (java_argument) (*environment)->DeleteLocalRef(environment, java_argument);
    if (activity_class) (*environment)->DeleteLocalRef(environment, activity_class);
    (*environment)->DeleteLocalRef(environment, activity);
    return error[0] == 0;
}

int android_bridge_initialize(void)
{
    const char *directory = SDL_GetAndroidInternalStoragePath();
    if (!directory || !*directory) {
        fprintf(stderr, "android: cannot resolve app-private storage: %s\n", SDL_GetError());
        return 0;
    }
    if (!lucent_platform_set_user_data_directory(directory)) {
        fprintf(stderr, "android: Lucent refused app-private storage path %s\n", directory);
        return 0;
    }
    return ensure_selection_state();
}

int android_bridge_game_root(char *output, size_t capacity)
{
    const char *directory = SDL_GetAndroidInternalStoragePath();
    if (!directory || !*directory || !output || capacity == 0) return 0;
    const int written = snprintf(output, capacity, "%s/game", directory);
    return written >= 0 && (size_t)written < capacity;
}

SetupUiResult android_bridge_choose_game_tree(const char *message, char *output, size_t capacity)
{
    if (!ensure_selection_state() || !output || capacity == 0) return SETUP_UI_ERROR;
    output[0] = 0;

    SDL_LockMutex(selection.mutex);
    if (selection.waiting) {
        SDL_UnlockMutex(selection.mutex);
        fprintf(stderr, "android: a game-tree selection is already active\n");
        return SETUP_UI_ERROR;
    }
    selection.waiting = 1;
    selection.complete = 0;
    selection.failed = 0;
    selection.executable[0] = 0;
    selection.error[0] = 0;
    SDL_UnlockMutex(selection.mutex);

    char call_error[512] = "";
    if (!call_activity_void_string("requestLf2GameTree", message, call_error, sizeof call_error)) {
        SDL_LockMutex(selection.mutex);
        selection.waiting = 0;
        SDL_UnlockMutex(selection.mutex);
        fprintf(stderr, "android: %s\n", call_error);
        return SETUP_UI_ERROR;
    }

    SDL_LockMutex(selection.mutex);
    enum { SELECTION_TIMEOUT_MS = 30 * 60 * 1000 };
    const Uint64 deadline = SDL_GetTicks() + SELECTION_TIMEOUT_MS;
    while (!selection.complete) {
        const Uint64 now = SDL_GetTicks();
        if (now >= deadline ||
            !SDL_WaitConditionTimeout(selection.condition, selection.mutex, (Sint32)(deadline - now))) {
            selection.failed = 1;
            selection.complete = 1;
            snprintf(selection.error, sizeof selection.error,
                     "the Android game-file picker did not complete within 30 minutes");
        }
    }
    const int failed = selection.failed;
    const int selected = selection.executable[0] != 0;
    if (selected && !copy_text(output, capacity, selection.executable)) {
        fprintf(stderr, "android: imported game path is longer than %zu bytes\n", capacity - 1);
        output[0] = 0;
    }
    if (failed) fprintf(stderr, "android: game-tree import failed: %s\n", selection.error);
    selection.waiting = 0;
    SDL_UnlockMutex(selection.mutex);
    if (failed || (selected && !output[0])) return SETUP_UI_ERROR;
    return selected ? SETUP_UI_SELECTED : SETUP_UI_CANCELLED;
}

int android_bridge_commit_game_tree(const char *staging_root, char *output, size_t capacity, char *error,
                                    size_t error_capacity)
{
    if (!staging_root || !*staging_root || !output || capacity == 0 || !error || error_capacity == 0) return 0;
    output[0] = 0;
    error[0] = 0;

    JNIEnv *environment = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jobject activity = (jobject)SDL_GetAndroidActivity();
    if (!environment || !activity) {
        snprintf(error, error_capacity, "Android Activity is not available while accepting the game tree");
        return 0;
    }
    jclass activity_class = (*environment)->GetObjectClass(environment, activity);
    jmethodID method = activity_class ? (*environment)
                                            ->GetMethodID(environment, activity_class, "commitLf2GameTree",
                                                          "(Ljava/lang/String;)Ljava/lang/String;")
                                      : NULL;
    jstring java_staging = (*environment)->NewStringUTF(environment, staging_root);
    jstring java_root = NULL;
    if (activity_class && method && java_staging)
        java_root = (jstring)(*environment)->CallObjectMethod(environment, activity, method, java_staging);
    if ((*environment)->ExceptionCheck(environment)) {
        (*environment)->ExceptionClear(environment);
        snprintf(error, error_capacity, "Android could not atomically accept the validated game tree");
    } else if (!java_root) {
        snprintf(error, error_capacity, "Android did not return the accepted private game-tree path");
    } else {
        const char *root = (*environment)->GetStringUTFChars(environment, java_root, NULL);
        if (!root || !copy_text(output, capacity, root))
            snprintf(error, error_capacity, "accepted Android game-tree path is longer than %zu bytes", capacity - 1);
        if (root) (*environment)->ReleaseStringUTFChars(environment, java_root, root);
    }
    if (java_root) (*environment)->DeleteLocalRef(environment, java_root);
    if (java_staging) (*environment)->DeleteLocalRef(environment, java_staging);
    if (activity_class) (*environment)->DeleteLocalRef(environment, activity_class);
    (*environment)->DeleteLocalRef(environment, activity);
    return error[0] == 0;
}

JNIEXPORT void JNICALL Java_io_github_someoneisworking_lf2port_Lf2Activity_nativeGameTreeResult(JNIEnv *environment,
                                                                                                jclass activity_class,
                                                                                                jstring executable,
                                                                                                jstring error)
{
    (void)activity_class;
    if (!ensure_selection_state()) return;
    SDL_LockMutex(selection.mutex);
    if (selection.waiting) {
        selection.executable[0] = 0;
        selection.error[0] = 0;
        if (executable) {
            const char *value = (*environment)->GetStringUTFChars(environment, executable, NULL);
            if (value) {
                if (!copy_text(selection.executable, sizeof selection.executable, value)) {
                    selection.failed = 1;
                    snprintf(selection.error, sizeof selection.error, "the imported game path is too long");
                }
                (*environment)->ReleaseStringUTFChars(environment, executable, value);
            }
        }
        if (error) {
            const char *value = (*environment)->GetStringUTFChars(environment, error, NULL);
            if (value) {
                selection.failed = 1;
                copy_text(selection.error, sizeof selection.error, value);
                (*environment)->ReleaseStringUTFChars(environment, error, value);
            }
        }
        selection.complete = 1;
        SDL_BroadcastCondition(selection.condition);
    }
    SDL_UnlockMutex(selection.mutex);
}

void android_bridge_finish_activity(void)
{
    char error[256] = "";
    call_activity_void_string("finishApp", "", error, sizeof error);
}
