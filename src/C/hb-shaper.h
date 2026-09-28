#pragma once

#include <pango/pango.h>
#include <gsk/gsk.h>

typedef struct {
    PangoGlyphInfo *glyphs;   /* owned array, ready for gsk_text_node_new */
    uint32_t       *clusters; /* byte offset per glyph, for color lookup  */
    PangoFont      *font;     /* ref held */
    int             n_glyphs;
    int             width;    /* pango units */
} HbShapedRun;

typedef struct {
    int         width_px;
    int         height_px;
    int         baseline_px;
    int         n_runs;
    HbShapedRun runs[];
} HbShaped;

void      hb_shaper_init(PangoFontMap *font_map);
HbShaped *hb_shaper_shape(PangoFont *font, const char *text);
void      hb_shaped_free(HbShaped *s);
PangoFont *hb_shaper_resolve_font(const PangoFontDescription *desc);
