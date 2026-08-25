#pragma once

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LF2_LOG_INFO,
    LF2_LOG_WARNING,
    LF2_LOG_ERROR,
} Lf2LogLevel;

int lf2_log_fprintf(FILE *stream, const char *source, const char *format, ...);
int lf2_log_printf(const char *source, const char *format, ...);
int lf2_log_fputs(const char *source, const char *text, FILE *stream);
void lf2_log_perror(const char *source, const char *message);
void lf2_log_write(Lf2LogLevel level, const char *channel, const char *message);
void lf2_log_flush(void);

#ifdef __cplusplus
}
#endif

// This header is force-included for the shipping target. Existing C diagnostics therefore reach
// Lucent without a second printf implementation or hundreds of wrappers that could drift. File
// output is preserved verbatim; only stdout/stderr are process logs.
#define fprintf(stream, ...) lf2_log_fprintf((stream), __FILE__, __VA_ARGS__)
#define printf(...) lf2_log_printf(__FILE__, __VA_ARGS__)
#define fputs(...) lf2_log_fputs(__FILE__, __VA_ARGS__)
#define perror(message) lf2_log_perror(__FILE__, (message))
