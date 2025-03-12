#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <gdk/gdk.h>
#include "highlight.h"

#include <gdk/gdk.h>
#include <string.h>
#include <stdio.h>

static char match_highlight[64] = {0}; // Keep this for backward compatibility
static GdkRGBA accent_rgba = {1.0, 0.5, 0.8, 0.3}; // Default black, fully opaque

GdkRGBA* highlight_get_accent_color() {
    return &accent_rgba;
}

const char* highlight_get_pango_accent() {
    return match_highlight;
}

static void highlight_parse_color(const char* color, GdkRGBA* rgba) {
    if (!color || !rgba) return;

    if (gdk_rgba_parse(rgba, color)) {
        return;
    }

    unsigned int r = 0, g = 0, b = 0, a = 255;

    if (color[0] == '#') {
        // Hex format (#RRGGBB or #RRGGBBAA)
        if (strlen(color) >= 9) {
            sscanf(color + 1, "%2x%2x%2x%2x", &r, &g, &b, &a);
        }
        else if (strlen(color) >= 7) {
            sscanf(color + 1, "%2x%2x%2x", &r, &g, &b);
        }
    }
    else if (strncmp(color, "rgb(", 4) == 0) {
        // RGB format: rgb(r, g, b)
        sscanf(color, "rgb(%u, %u, %u)", &r, &g, &b);
    }
    else if (strncmp(color, "rgba(", 5) == 0) {
        // RGBA format: rgba(r, g, b, a)
        float alpha_float = 1.0;
        sscanf(color, "rgba(%u, %u, %u, %f)", &r, &g, &b, &alpha_float);
        a = (unsigned int)(alpha_float * 255.0 + 0.5);
    }

    rgba->red = r / 255.0;
    rgba->green = g / 255.0;
    rgba->blue = b / 255.0;
    rgba->alpha = a / 255.0;
}

static void highlight_format_accent_color(const GdkRGBA* rgba) {
    if (!rgba) return;

    // Convert to 0-255 range for formatting
    unsigned int r = (unsigned int)(rgba->red * 255.0 + 0.5);
    unsigned int g = (unsigned int)(rgba->green * 255.0 + 0.5);
    unsigned int b = (unsigned int)(rgba->blue * 255.0 + 0.5);
    int pango_alpha = (int)(rgba->alpha * 100.0 + 0.5);

    // Format the Pango markup string
    snprintf(match_highlight, sizeof(match_highlight),
             "<span foreground=\"#%02x%02x%02x\" alpha=\"%d%%\">",
             r, g, b, pango_alpha);
}

void highlight_set_accent_color(const char* color) {
    if (!color) return;

    // Parse the color into our static GdkRGBA
    highlight_parse_color(color, &accent_rgba);

    // Format the Pango markup string for backward compatibility
    highlight_format_accent_color(&accent_rgba);
}


int* match_positions_with_markup(needle_info* needle, const char* haystack, int* result_length) {
    if (!needle || !haystack) return NULL;

    int n = needle->len;
    size_t len = (n + 1 < MATCH_MAX_LEN) ? n + 1 : MATCH_MAX_LEN;

    int* positions = malloc(len * sizeof(int));
    if (!positions) return NULL;

    // Fill the array with -1
    memset(positions, -1, len * sizeof(int));

    match_positions(needle, haystack, positions);

    *result_length = len;

    return positions;
}



// String builder for efficient string concatenation
struct string_builder {
    char* buf;
    size_t len;
    size_t cap;
};

static struct string_builder* sb_new(size_t initial_cap) {
    struct string_builder* sb = malloc(sizeof(struct string_builder));
    if (!sb) return NULL;

    sb->buf = malloc(initial_cap);
    if (!sb->buf) {
        free(sb);
        return NULL;
    }

    sb->len = 0;
    sb->cap = initial_cap;
    sb->buf[0] = '\0';
    return sb;
}

static int sb_ensure_space(struct string_builder* sb, size_t additional) {
    if (sb->len + additional + 1 > sb->cap) {
        size_t new_cap = sb->cap * 2;
        while (new_cap < sb->len + additional + 1) {
            new_cap *= 2;
        }

        char* new_buf = realloc(sb->buf, new_cap);
        if (!new_buf) return 0;

        sb->buf = new_buf;
        sb->cap = new_cap;
    }
    return 1;
}

static int sb_append(struct string_builder* sb, const char* str) {
    size_t len = strlen(str);
    if (!sb_ensure_space(sb, len)) return 0;

    memcpy(sb->buf + sb->len, str, len);
    sb->len += len;
    sb->buf[sb->len] = '\0';
    return 1;
}

static void append_pango_escaped_char(struct string_builder* sb, uint32_t c) {
    switch (c) {
        case '&':
            sb_append(sb, "&amp;");
            break;
        case '<':
            sb_append(sb, "&lt;");
            break;
        case '>':
            sb_append(sb, "&gt;");
            break;
        case '\'':
            sb_append(sb, "&apos;");
            break;
        case '"':
            sb_append(sb, "&quot;");
            break;
        default: {
            char buf[8];  // UTF-8 needs at most 4 bytes + null
            if (c < 0x80) {
                buf[0] = (char)c;
                buf[1] = '\0';
            } else {
                size_t len = 0;
                if (c < 0x800) {
                    buf[len++] = 0xC0 | (c >> 6);
                    buf[len++] = 0x80 | (c & 0x3F);
                } else if (c < 0x10000) {
                    buf[len++] = 0xE0 | (c >> 12);
                    buf[len++] = 0x80 | ((c >> 6) & 0x3F);
                    buf[len++] = 0x80 | (c & 0x3F);
                } else {
                    buf[len++] = 0xF0 | (c >> 18);
                    buf[len++] = 0x80 | ((c >> 12) & 0x3F);
                    buf[len++] = 0x80 | ((c >> 6) & 0x3F);
                    buf[len++] = 0x80 | (c & 0x3F);
                }
                buf[len] = '\0';
            }
            sb_append(sb, buf);
            break;
        }
    }
}

// Get next UTF-8 character and its length
static size_t get_next_utf8(const char* text, uint32_t* c) {
    if (!text || !*text) {
        *c = 0;
        return 0;
    }

    unsigned char first = (unsigned char)*text;
    if (first < 0x80) {
        *c = first;
        return 1;
    }

    size_t len = 0;
    uint32_t ch = 0;

    if ((first & 0xE0) == 0xC0) {
        len = 2;
        ch = first & 0x1F;
    } else if ((first & 0xF0) == 0xE0) {
        len = 3;
        ch = first & 0x0F;
    } else if ((first & 0xF8) == 0xF0) {
        len = 4;
        ch = first & 0x07;
    } else {
        *c = 0xFFFD;  // Replacement character
        return 1;
    }

    for (size_t i = 1; i < len; i++) {
        if ((text[i] & 0xC0) != 0x80) {
            *c = 0xFFFD;
            return 1;
        }
        ch = (ch << 6) | (text[i] & 0x3F);
    }

    *c = ch;
    return len;
}

char* highlight_apply_highlights(const char* text, const char* highlight_color, const int* positions, size_t positions_len) {
    if (!text) return NULL;
    if (!positions) return strdup(text);

    struct string_builder* sb = sb_new(strlen(text) * 2);  // Initial estimate
    if (!sb) return NULL;

    size_t pos_idx = 0;
    int in_highlight = 0;
    int counter = 0;

    const char* p = text;
    while (*p) {
        uint32_t c;
        size_t char_len = get_next_utf8(p, &c);
        if (char_len == 0) break;

        if (pos_idx < positions_len && positions[pos_idx] == counter) {
            pos_idx++;
            if (!in_highlight) {
                sb_append(sb, highlight_color);
                in_highlight = 1;
            }
        } else if (in_highlight) {
            sb_append(sb, "</span>");
            in_highlight = 0;
        }

        append_pango_escaped_char(sb, c);
        counter++;
        p += char_len;
    }

    if (in_highlight) {
        sb_append(sb, "</span>");
    }

    char* result = sb->buf;
    free(sb);
    return result;
}

char* highlight_format_highlights(const char* text, const char* highlight_color, needle_info* si) {
    if (!text || !si) return NULL;

    int positions_length = 0;

    int* positions = match_positions_with_markup(si, text, &positions_length);
    if (!positions) return strdup(text);

    char* result = highlight_apply_highlights(text, highlight_color, positions, positions_length);
    free(positions);
    return result;
}
