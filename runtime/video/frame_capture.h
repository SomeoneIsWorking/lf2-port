#ifndef LF2_FRAME_CAPTURE_H
#define LF2_FRAME_CAPTURE_H
#include <stddef.h>
#include <stdint.h>
void frame_capture_path(char *out, size_t size, const char *format, ...);
typedef struct {
    const uint8_t *pixels;
    int width;
    int height;
    int pitch;
    long number;
} CaptureFrame;
void frame_capture_present(CaptureFrame frame);
#endif
