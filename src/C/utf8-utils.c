#include "utf8-utils.h"
#include <string.h>

static bool is_variation_selector_16(const char* str, size_t pos) {
    size_t len = strlen(str);
    return pos + 2 < len &&
           (unsigned char)str[pos] == 0xEF &&
           (unsigned char)str[pos + 1] == 0xB8 &&
           (unsigned char)str[pos + 2] == 0x8F;
}

static bool is_skin_tone_modifier(const char* str, size_t pos) {
    size_t len = strlen(str);
    if (pos + 3 >= len) return false;
    if ((unsigned char)str[pos] != 0xF0 || (unsigned char)str[pos + 1] != 0x9F) return false;
    if ((unsigned char)str[pos + 2] != 0x8F) return false;
    unsigned char last = (unsigned char)str[pos + 3];
    return last >= 0xBB && last <= 0xBF;
}

static bool is_zwj(const char* str, size_t pos) {
    size_t len = strlen(str);
    return pos + 2 < len &&
           (unsigned char)str[pos] == 0xE2 &&
           (unsigned char)str[pos + 1] == 0x80 &&
           (unsigned char)str[pos + 2] == 0x8D;
}

static bool is_emoji_base(const char* str, size_t pos) {
    size_t len = strlen(str);
    if (pos + 3 >= len) return false;
    unsigned char b1 = (unsigned char)str[pos];
    unsigned char b2 = (unsigned char)str[pos + 1];

    if (b1 == 0xF0 && b2 == 0x9F) {
        unsigned char b3 = (unsigned char)str[pos + 2];
        return (b3 >= 0x98 && b3 <= 0x9F) ||
               (b3 >= 0x8C && b3 <= 0x97) ||
               (b3 >= 0x9A && b3 <= 0x9B);
    }

    if (b1 >= 0xE2) {
        return (b1 == 0xE2 && b2 >= 0x98) ||
               (b1 == 0xE2 && b2 <= 0x9C) ||
               (b1 >= 0xE2 && b1 <= 0xE3);
    }

    return false;
}

static bool is_combining_mark(const char* str, size_t pos) {
    size_t len = strlen(str);
    if (pos + 2 >= len) return false;

    unsigned char b1 = (unsigned char)str[pos];
    unsigned char b2 = (unsigned char)str[pos + 1];

    if (b1 == 0xCC || b1 == 0xCD) return true;
    if (b1 == 0xE2 && b2 == 0x83) return true;

    return false;
}

size_t utf8_char_byte_length(const char* str, size_t byte_pos) {
    if (!str) return 0;

    unsigned char c = (unsigned char)str[byte_pos];
    if (c == 0) return 0;
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static size_t next_grapheme_boundary(const char* str, size_t start_pos) {
    if (!str) return 0;

    size_t len = strlen(str);
    if (start_pos >= len) return len;

    size_t pos = start_pos;
    size_t char_len = utf8_char_byte_length(str, pos);
    if (char_len == 0) return start_pos;

    bool is_emoji = is_emoji_base(str, pos);
    pos += char_len;

    while (pos < len) {
        if (is_variation_selector_16(str, pos)) {
            pos += 3;
            continue;
        }

        if (is_combining_mark(str, pos)) {
            size_t mark_len = utf8_char_byte_length(str, pos);
            pos += mark_len;
            continue;
        }

        if (is_emoji && is_skin_tone_modifier(str, pos)) {
            pos += 4;
            continue;
        }

        if (is_zwj(str, pos)) {
            pos += 3;
            if (pos < len) {
                size_t next_len = utf8_char_byte_length(str, pos);
                if (next_len > 0) {
                    pos += next_len;
                    continue;
                }
            }
        }

        break;
    }

    return pos;
}

size_t utf8_char_count(const char* str) {
    if (!str) return 0;

    size_t count = 0;
    size_t pos = 0;
    size_t len = strlen(str);

    while (pos < len) {
        size_t next_pos = next_grapheme_boundary(str, pos);
        if (next_pos == pos) {
            pos += utf8_char_byte_length(str, pos);
            if (pos > len) break;
        } else {
            pos = next_pos;
        }
        count++;
    }

    return count;
}

size_t utf8_char_to_byte_pos(const char* str, size_t char_pos) {
    if (!str || char_pos == 0) return 0;

    size_t pos = 0;
    size_t len = strlen(str);

    for (size_t i = 0; i < char_pos && pos < len; i++) {
        size_t next_pos = next_grapheme_boundary(str, pos);
        if (next_pos == pos) {
            pos += utf8_char_byte_length(str, pos);
            if (pos > len) break;
        } else {
            pos = next_pos;
        }
    }

    return pos;
}

size_t utf8_byte_to_char_pos(const char* str, size_t byte_pos) {
    if (!str) return 0;

    size_t char_pos = 0;
    size_t pos = 0;
    size_t len = strlen(str);

    while (pos < len && pos < byte_pos) {
        size_t next_pos = next_grapheme_boundary(str, pos);
        if (next_pos == pos) {
            pos += utf8_char_byte_length(str, pos);
            if (pos > len) break;
        } else {
            pos = next_pos;
        }
        char_pos++;
    }

    return char_pos;
}

size_t utf8_move_chars(const char* str, size_t current_byte_pos, int char_delta) {
    if (!str) return 0;

    size_t current_char_pos = utf8_byte_to_char_pos(str, current_byte_pos);
    size_t target_char_pos;

    if (char_delta < 0 && (size_t)(-char_delta) > current_char_pos) {
        target_char_pos = 0;
    } else {
        target_char_pos = current_char_pos + char_delta;
    }

    return utf8_char_to_byte_pos(str, target_char_pos);
}
