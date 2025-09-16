#pragma once

#include <gdk/gdk.h>
#include <pango/pango.h>
#include <stddef.h>
#include <stdint.h>
#include "match.h"

typedef enum {
    HIGHLIGHT_STYLE_COLOR      = 1 << 0,
    HIGHLIGHT_STYLE_UNDERLINE  = 1 << 1,
    HIGHLIGHT_STYLE_BOLD       = 1 << 2,
    HIGHLIGHT_STYLE_BACKGROUND = 1 << 3,
    HIGHLIGHT_STYLE_STRIKETHROUGH = 1 << 4,
} HighlightStyle;

typedef struct {
    size_t* byte_ranges;  // Pairs of start/end byte positions
    size_t range_count;   // Number of ranges (pairs)
} HighlightPositions;


// ========== Core Accent Color Management ==========

/**
 * Set the global accent color used for highlighting
 * @param color Color string in various formats (#RRGGBB, rgb(), rgba(), etc.)
 */
void highlight_set_accent_color(const char* color);

/**
 * Get the current accent color
 * @return Pointer to the global accent color (do not free)
 */
GdkRGBA* highlight_get_accent_color(void);

/**
 * Get the Pango markup string for the accent color (backward compatibility)
 * @return Pango markup opening tag (do not free)
 */
const char* highlight_get_pango_accent(void);

// ========== New Attribute-based API ==========

/**
 * Calculate highlight positions for text (expensive operation)
 * @param needle The search pattern info
 * @param text The text to highlight
 * @return Positions structure (must be freed with highlight_positions_free)
 */
HighlightPositions* highlight_calculate_positions(needle_info* needle, const char* text);

/**
 * Apply styling to pre-calculated positions (cheap operation)
 * @param positions The pre-calculated positions
 * @param style Combination of HighlightStyle flags
 * @param color Color to use for styling (can be NULL for some styles)
 * @return New PangoAttrList (caller owns and must free)
 */
PangoAttrList* highlight_apply_style(const HighlightPositions* positions,
                                     HighlightStyle style,
                                     const GdkRGBA* color);

PangoAttrList* highlight_apply_style_range(const HighlightPositions* positions,
                                          HighlightStyle style,
                                          const GdkRGBA* color,
                                          size_t range_start_byte,
                                          size_t range_end_byte);

/**
 * Convenience function: calculate positions and apply style in one call
 * @param needle The search pattern info
 * @param text The text to highlight
 * @param style Combination of HighlightStyle flags
 * @return New PangoAttrList (caller owns and must free)
 */
PangoAttrList* highlight_create_attr_list(needle_info* needle,
                                          const char* text,
                                          HighlightStyle style);

/**
 * Free a positions structure
 * @param positions The positions to free (can be NULL)
 */
void highlight_positions_free(HighlightPositions* positions);

// Maximum length for match arrays (should match your existing code)
#ifndef MATCH_MAX_LEN
#define MATCH_MAX_LEN 256
#endif
