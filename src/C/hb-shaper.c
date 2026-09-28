#include "hb-shaper.h"
#include <harfbuzz/hb.h>
#include <string.h>
#include <stdlib.h>

static PangoFontMap *shared_font_map;
static __thread PangoContext *tl_ctx;

static PangoContext *
get_thread_ctx(void)
{
    if (!tl_ctx)
        tl_ctx = pango_font_map_create_context(shared_font_map);
    return tl_ctx;
}

void
hb_shaper_init(PangoFontMap *font_map)
{
    shared_font_map = font_map;
}

PangoFont *
hb_shaper_resolve_font(const PangoFontDescription *desc)
{
    return pango_font_map_load_font(shared_font_map, get_thread_ctx(), desc);
}

HbShaped *
hb_shaper_shape(PangoFont *font, const char *text)
{
    hb_font_t *hb_font = pango_font_get_hb_font(font);

    hb_font_extents_t ext;
    hb_font_get_h_extents(hb_font, &ext);

    int len = (int)strlen(text);
    if (len == 0) {
        HbShaped *s   = malloc(sizeof(*s));
        s->n_runs     = 0;
        s->width_px   = 0;
        s->height_px  = PANGO_PIXELS_CEIL(ext.ascender - ext.descender);
        s->baseline_px = PANGO_PIXELS_CEIL(ext.ascender);
        return s;
    }

    hb_buffer_t *buf = hb_buffer_create();
    hb_buffer_add_utf8(buf, text, len, 0, len);
    hb_buffer_guess_segment_properties(buf);
    hb_shape(hb_font, buf, NULL, 0);

    unsigned int n;
    hb_glyph_info_t     *gi = hb_buffer_get_glyph_infos(buf, &n);
    hb_glyph_position_t *gp = hb_buffer_get_glyph_positions(buf, &n);

    HbShaped *s = malloc(sizeof(*s) + sizeof(HbShapedRun));
    s->n_runs = 1;

    HbShapedRun *run = &s->runs[0];
    run->font     = g_object_ref(font);
    run->n_glyphs = (int)n;
    run->glyphs   = g_new(PangoGlyphInfo, n);
    run->clusters = g_new(uint32_t, n);

    int total_advance = 0;
    for (unsigned i = 0; i < n; i++) {
        PangoGlyphInfo *pg = &run->glyphs[i];
        pg->glyph              = gi[i].codepoint;
        pg->geometry.width     = gp[i].x_advance;
        pg->geometry.x_offset  = gp[i].x_offset;
        pg->geometry.y_offset  = gp[i].y_offset;
        pg->attr.is_cluster_start =
            (i == 0 || gi[i].cluster != gi[i - 1].cluster);
        pg->attr.is_color = 0;

        run->clusters[i] = gi[i].cluster;
        total_advance += gp[i].x_advance;
    }
    run->width = total_advance;

    s->width_px    = PANGO_PIXELS_CEIL(total_advance);
    s->height_px   = PANGO_PIXELS_CEIL(ext.ascender - ext.descender);
    s->baseline_px = PANGO_PIXELS_CEIL(ext.ascender);

    hb_buffer_destroy(buf);
    return s;
}

void
hb_shaped_free(HbShaped *s)
{
    if (!s) return;
    for (int i = 0; i < s->n_runs; i++) {
        g_free(s->runs[i].glyphs);
        g_free(s->runs[i].clusters);
        g_object_unref(s->runs[i].font);
    }
    free(s);
}
