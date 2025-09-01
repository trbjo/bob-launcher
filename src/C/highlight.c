#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <gdk/gdk.h>
#include <pango/pango.h>
#include "highlight.h"

// Static storage for accent color
static GdkRGBA accent_rgba = {1.0, 0.5, 0.8, 0.3}; // Default
static char match_highlight[64] = {0}; // Keep for backward compatibility

static void highlight_parse_color(const char* color, GdkRGBA* rgba) {
    if (!color || !rgba) return;

    if (gdk_rgba_parse(rgba, color)) {
        return;
    }

    unsigned int r = 0, g = 0, b = 0, a = 255;

    if (color[0] == '#') {
        if (strlen(color) >= 9) {
            sscanf(color + 1, "%2x%2x%2x%2x", &r, &g, &b, &a);
        }
        else if (strlen(color) >= 7) {
            sscanf(color + 1, "%2x%2x%2x", &r, &g, &b);
        }
    }
    else if (strncmp(color, "rgb(", 4) == 0) {
        sscanf(color, "rgb(%u, %u, %u)", &r, &g, &b);
    }
    else if (strncmp(color, "rgba(", 5) == 0) {
        float alpha_float = 1.0;
        sscanf(color, "rgba(%u, %u, %u, %f)", &r, &g, &b, &alpha_float);
        a = (unsigned int)(alpha_float * 255.0 + 0.5);
    }

    rgba->red = r / 255.0;
    rgba->green = g / 255.0;
    rgba->blue = b / 255.0;
    rgba->alpha = a / 255.0;
}


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


// Format accent color for Pango markup (backward compatibility)
static void highlight_format_accent_color(const GdkRGBA* rgba) {
    if (!rgba) return;

    unsigned int r = (unsigned int)(rgba->red * 255.0 + 0.5);
    unsigned int g = (unsigned int)(rgba->green * 255.0 + 0.5);
    unsigned int b = (unsigned int)(rgba->blue * 255.0 + 0.5);
    int pango_alpha = (int)(rgba->alpha * 100.0 + 0.5);

    snprintf(match_highlight, sizeof(match_highlight),
             "<span foreground=\"#%02x%02x%02x\" alpha=\"%d%%\">",
             r, g, b, pango_alpha);
}

// Public API - Get accent color
GdkRGBA* highlight_get_accent_color() {
    return &accent_rgba;
}

// Public API - Get Pango accent string (backward compatibility)
const char* highlight_get_pango_accent() {
    return match_highlight;
}

// Public API - Set accent color
void highlight_set_accent_color(const char* color) {
    if (!color) return;
    highlight_parse_color(color, &accent_rgba);
    highlight_format_accent_color(&accent_rgba);
}

// Get match positions
int* match_positions_with_markup(needle_info* needle, const char* haystack, int* result_length) {
    if (!needle || !haystack) return NULL;

    int n = needle->len;
    size_t len = (n + 1 < MATCH_MAX_LEN) ? n + 1 : MATCH_MAX_LEN;

    int* positions = malloc(len * sizeof(int));
    if (!positions) return NULL;

    memset(positions, -1, len * sizeof(int));
    match_positions(needle, haystack, positions);

    *result_length = len;
    return positions;
}

HighlightPositions* highlight_calculate_positions(needle_info* needle, const char* text) {
    if (!needle || !text) return NULL;

    int positions_length = 0;
    int* char_positions = match_positions_with_markup(needle, text, &positions_length);
    if (!char_positions) return NULL;

    HighlightPositions* result = malloc(sizeof(HighlightPositions));
    if (!result) {
        free(char_positions);
        return NULL;
    }

    // Pre-allocate for worst case
    result->byte_ranges = malloc(positions_length * 2 * sizeof(size_t));
    result->range_count = 0;

    // Convert character positions to byte ranges
    size_t pos_idx = 0;
    int char_counter = 0;
    gboolean in_highlight = FALSE;
    size_t highlight_start_byte = 0;

    const char* p = text;
    size_t byte_pos = 0;

    while (*p && pos_idx < positions_length) {
        uint32_t c;
        size_t char_len = get_next_utf8(p, &c);
        if (char_len == 0) break;

        if (char_positions[pos_idx] == char_counter) {
            if (!in_highlight) {
                highlight_start_byte = byte_pos;
                in_highlight = TRUE;
            }
            pos_idx++;
        } else if (in_highlight) {
            // Store the range
            result->byte_ranges[result->range_count * 2] = highlight_start_byte;
            result->byte_ranges[result->range_count * 2 + 1] = byte_pos;
            result->range_count++;
            in_highlight = FALSE;
        }

        char_counter++;
        byte_pos += char_len;
        p += char_len;
    }

    // Handle case where highlight extends to end
    if (in_highlight) {
        result->byte_ranges[result->range_count * 2] = highlight_start_byte;
        result->byte_ranges[result->range_count * 2 + 1] = byte_pos;
        result->range_count++;
    }

    free(char_positions);

    // Shrink to actual size
    if (result->range_count > 0) {
        result->byte_ranges = realloc(result->byte_ranges,
                                      result->range_count * 2 * sizeof(size_t));
    } else {
        free(result->byte_ranges);
        result->byte_ranges = NULL;
    }

    return result;
}

PangoAttrList* highlight_apply_style_range(const HighlightPositions* positions,
                                          HighlightStyle style,
                                          const GdkRGBA* color,
                                          size_t range_start_byte,
                                          size_t range_end_byte) {
    PangoAttrList* attr_list = pango_attr_list_new();
    if (!positions || !positions->byte_ranges) return attr_list;

    for (size_t i = 0; i < positions->range_count; i++) {
        size_t start = positions->byte_ranges[i * 2];
        size_t end = positions->byte_ranges[i * 2 + 1];

        // Check if this range intersects with our desired range
        if (end <= range_start_byte || start >= range_end_byte) {
            continue; // No intersection, skip this range
        }

        // Calculate intersection and adjust to range coordinates
        size_t adjusted_start = (start > range_start_byte) ? start - range_start_byte : 0;
        size_t adjusted_end = (end < range_end_byte) ? end - range_start_byte : range_end_byte - range_start_byte;

        if (style & HIGHLIGHT_STYLE_COLOR) {
            PangoAttribute* attr = pango_attr_foreground_new(
                (guint16)(color->red * 65535),
                (guint16)(color->green * 65535),
                (guint16)(color->blue * 65535)
            );
            attr->start_index = adjusted_start;
            attr->end_index = adjusted_end;
            pango_attr_list_insert(attr_list, attr);

            if (color->alpha < 1.0) {
                PangoAttribute* alpha_attr = pango_attr_foreground_alpha_new(
                    (guint16)(color->alpha * 65535)
                );
                alpha_attr->start_index = adjusted_start;
                alpha_attr->end_index = adjusted_end;
                pango_attr_list_insert(attr_list, alpha_attr);
            }
        }

        if (style & HIGHLIGHT_STYLE_UNDERLINE) {
            PangoAttribute* attr = pango_attr_underline_new(PANGO_UNDERLINE_SINGLE);
            attr->start_index = adjusted_start;
            attr->end_index = adjusted_end;
            pango_attr_list_insert(attr_list, attr);
        }

        if (style & HIGHLIGHT_STYLE_BOLD) {
            PangoAttribute* attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
            attr->start_index = adjusted_start;
            attr->end_index = adjusted_end;
            pango_attr_list_insert(attr_list, attr);
        }

        if (style & HIGHLIGHT_STYLE_BACKGROUND) {
            PangoAttribute* attr = pango_attr_background_new(
                (guint16)(color->red * 65535),
                (guint16)(color->green * 65535),
                (guint16)(color->blue * 65535)
            );
            attr->start_index = adjusted_start;
            attr->end_index = adjusted_end;
            pango_attr_list_insert(attr_list, attr);

            if (color->alpha < 1.0) {
                PangoAttribute* alpha_attr = pango_attr_background_alpha_new(
                    (guint16)(color->alpha * 65535)
                );
                alpha_attr->start_index = adjusted_start;
                alpha_attr->end_index = adjusted_end;
                pango_attr_list_insert(attr_list, alpha_attr);
            }
        }

        if (style & HIGHLIGHT_STYLE_STRIKETHROUGH) {
            PangoAttribute* attr = pango_attr_strikethrough_new(TRUE);
            attr->start_index = adjusted_start;
            attr->end_index = adjusted_end;
            pango_attr_list_insert(attr_list, attr);
        }
    }

    return attr_list;
}

PangoAttrList* highlight_apply_style(const HighlightPositions* positions,
                                     HighlightStyle style,
                                     const GdkRGBA* color) {
    return highlight_apply_style_range(positions, style, color, 0, SIZE_MAX);
}


PangoAttrList* highlight_create_attr_list(needle_info* needle, const char* text,
                                          HighlightStyle style) {
    HighlightPositions* positions = highlight_calculate_positions(needle, text);
    if (!positions) return pango_attr_list_new();

    PangoAttrList* attrs = highlight_apply_style(positions, style, &accent_rgba);
    highlight_positions_free(positions);
    return attrs;
}

void highlight_positions_free(HighlightPositions* positions) {
    if (positions) {
        free(positions->byte_ranges);
        free(positions);
    }
}
