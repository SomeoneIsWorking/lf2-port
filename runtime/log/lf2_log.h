#ifndef LF2_LOG_H
#define LF2_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LF2_LOG_INFO,
    LF2_LOG_WARNING,
    LF2_LOG_ERROR,
} Lf2LogLevel;

void lf2_log_perror(const char *channel, const char *message);
void lf2_log_write(Lf2LogLevel level, const char *channel, const char *message);
void lf2_log_writef(Lf2LogLevel level, const char *channel, const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif /* LF2_LOG_H */
