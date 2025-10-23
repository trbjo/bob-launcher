#ifndef UTF8_UTILS_H
#define UTF8_UTILS_H

#include <stddef.h>

size_t utf8_char_to_byte_pos(const char* str, size_t char_pos);
size_t utf8_byte_to_char_pos(const char* str, size_t byte_pos);
size_t utf8_char_byte_length(const char* str, size_t byte_pos);
size_t utf8_move_chars(const char* str, size_t current_byte_pos, int char_delta);
size_t utf8_char_count(const char* str);

#endif
