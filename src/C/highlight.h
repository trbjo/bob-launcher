#ifndef HIGHLIGHT_H
#define HIGHLIGHT_H

#include <stdint.h>
#include <stddef.h>
#include <gdk/gdk.h>
#include "match.h"

GdkRGBA* highlight_get_accent_color();
const char* highlight_get_pango_accent();

void highlight_set_accent_color(const char* color);

int* match_positions_with_markup(needle_info* needle, const char* haystack, int* result_length);
char* highlight_apply_highlights(const char* text, const char* highlight_color, const int* positions, size_t positions_len);
char* highlight_format_highlights(const char* text, const char* highlight_color, needle_info* si);

#endif // HIGHLIGHT_H
