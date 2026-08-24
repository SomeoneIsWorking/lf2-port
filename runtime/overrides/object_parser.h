#ifndef LF2_OBJECT_PARSER_H
#define LF2_OBJECT_PARSER_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *cursor;
    const char *end;
} ObjectTokenStream;

int object_token_next(ObjectTokenStream *stream, char *out, size_t capacity);
int object_token_int(ObjectTokenStream *stream, int32_t *out);
void object_parser_checksum_token(const char *token);
int object_parser_load_frames(uint32_t object, const char *text, size_t size);

#endif
