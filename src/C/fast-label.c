#include "fast-label.h"
#include "hb-shaper.h"
#include "thread-manager.h"
#include <stdatomic.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>

struct _BobLauncherFastLabel {
    GtkWidget parent_instance;
    PangoLayout *layout;        /* kept for text/attrs comparison + css_changed */
    HbShaped    *shaped;        /* worker result: metrics + glyph runs         */
    PangoFont   *resolved_font; /* resolved on backend thread for the worker   */
    char        *pending_text;  /* copied on backend thread for the worker     */
    int cached_width;
    int cached_height;
    int cached_baseline;
    atomic_int shaping;         /* 1 = worker in flight, 0 = done */

    /* Render-node cache: built once per shaping, replayed every snapshot.
     * Avoids gsk_text_node_new → gsk_get_glyph_string_extents → FreeType
     * per-glyph extent queries on every frame.                            */
    GskRenderNode **node_cache;
    int             n_cached_nodes;
    GdkRGBA         node_color;      /* color nodes were built with   */
    float           node_y;          /* baseline offset they used     */
};

struct _BobLauncherFastLabelClass {
    GtkWidgetClass parent_class;
};

static gpointer bob_launcher_fast_label_parent_class = NULL;

/* ==================================================================
 * Render-node cache helpers
 * ================================================================== */

static void
drop_node_cache(BobLauncherFastLabel *self)
{
    for (int i = 0; i < self->n_cached_nodes; i++)
        gsk_render_node_unref(self->node_cache[i]);
    g_free(self->node_cache);
    self->node_cache     = NULL;
    self->n_cached_nodes = 0;
}

/* ==================================================================
 * Shaping worker + helpers
 * ================================================================== */

static void
shape_worker(void *user_data)
{
    BobLauncherFastLabel *self = user_data;

    HbShaped *s = hb_shaper_shape(self->resolved_font, self->pending_text);

    if (self->shaped)
        hb_shaped_free(self->shaped);
    self->shaped = s;

    self->cached_width    = s->width_px;
    self->cached_height   = s->height_px;
    self->cached_baseline = s->baseline_px;

    /* Force node rebuild on next snapshot (new glyph data) */
    drop_node_cache(self);

    atomic_store_explicit(&self->shaping, 0, memory_order_release);
    syscall(SYS_futex, &self->shaping, FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0);
}

static inline void
wait_for_shaping(BobLauncherFastLabel *self)
{
    while (atomic_load_explicit(&self->shaping, memory_order_acquire))
        syscall(SYS_futex, &self->shaping, FUTEX_WAIT_PRIVATE, 1, NULL, NULL, 0);
}

static inline void
fire_shaping(BobLauncherFastLabel *self)
{
    atomic_store_explicit(&self->shaping, 1, memory_order_release);
    thread_pool_run(shape_worker, self, NULL);
}

static inline void
shape_sync(BobLauncherFastLabel *self)
{
    const char *text = pango_layout_get_text(self->layout);
    PangoContext *pctx = pango_layout_get_context(self->layout);
    const PangoFontDescription *fd = pango_context_get_font_description(pctx);

    if (self->resolved_font)
        g_object_unref(self->resolved_font);
    self->resolved_font = hb_shaper_resolve_font(fd);

    if (self->shaped)
        hb_shaped_free(self->shaped);
    self->shaped = hb_shaper_shape(self->resolved_font, text);

    self->cached_width    = self->shaped->width_px;
    self->cached_height   = self->shaped->height_px;
    self->cached_baseline = self->shaped->baseline_px;

    drop_node_cache(self);
}

/* Build GskTextNodes + decoration nodes from the HbShaped result.
 * Nodes are kept alive in node_cache[] and replayed on subsequent
 * snapshots until text, color, or baseline changes.
 *
 * Handles all highlight attributes produced by highlight.c:
 *   PANGO_ATTR_FOREGROUND + FOREGROUND_ALPHA  → per-span text color
 *   PANGO_ATTR_BACKGROUND + BACKGROUND_ALPHA  → colored rect behind text
 *   PANGO_ATTR_UNDERLINE                      → colored rect below baseline
 *   PANGO_ATTR_STRIKETHROUGH                  → colored rect at midline
 *   (PANGO_ATTR_WEIGHT/bold is ignored — would require a second font) */

typedef struct {
    int     start, end;       /* byte range in the original text          */
    GdkRGBA fg;               /* foreground color (base_color if unset)   */
    GdkRGBA bg;               /* background color                         */
    bool    has_fg;
    bool    has_bg;
    bool    underline;
    bool    strikethrough;
} StyleRange;

static void
rebuild_node_cache(BobLauncherFastLabel *self,
                   const GdkRGBA *base_color, float y)
{
    drop_node_cache(self);

    HbShaped *s = self->shaped;
    if (!s || s->n_runs == 0)
        return;

    PangoAttrList *attrs = pango_layout_get_attributes(self->layout);

    /* Worst case: 4 nodes per glyph (bg + text + underline + strikethrough) */
    int cap = 0;
    for (int r = 0; r < s->n_runs; r++)
        cap += s->runs[r].n_glyphs ? s->runs[r].n_glyphs : 1;
    GskRenderNode **nodes = g_new(GskRenderNode *, attrs ? cap * 4 : cap);
    int n = 0;

    for (int r = 0; r < s->n_runs; r++) {
        HbShapedRun *run = &s->runs[r];
        if (run->n_glyphs == 0)
            continue;

        /* ── No attrs → single text node, base color ────────── */
        if (!attrs) {
            PangoGlyphString gs = { run->n_glyphs, run->glyphs, NULL };
            GskRenderNode *node = gsk_text_node_new(
                run->font, &gs, base_color, &GRAPHENE_POINT_INIT(0, y));
            if (node)
                nodes[n++] = node;
            continue;
        }

        /* ── 1. Extract all style ranges from the attr list ─── */
        StyleRange ranges[32];
        int n_ranges = 0;

        PangoAttrIterator *iter = pango_attr_list_get_iterator(attrs);
        do {
            PangoAttribute *a_fg    = pango_attr_iterator_get(iter, PANGO_ATTR_FOREGROUND);
            PangoAttribute *a_fga   = pango_attr_iterator_get(iter, PANGO_ATTR_FOREGROUND_ALPHA);
            PangoAttribute *a_bg    = pango_attr_iterator_get(iter, PANGO_ATTR_BACKGROUND);
            PangoAttribute *a_bga   = pango_attr_iterator_get(iter, PANGO_ATTR_BACKGROUND_ALPHA);
            PangoAttribute *a_ul    = pango_attr_iterator_get(iter, PANGO_ATTR_UNDERLINE);
            PangoAttribute *a_st    = pango_attr_iterator_get(iter, PANGO_ATTR_STRIKETHROUGH);

            if (!a_fg && !a_bg && !a_ul && !a_st)
                continue;                              /* plain text span */
            if (n_ranges >= 32)
                break;

            int rs, re;
            pango_attr_iterator_range(iter, &rs, &re);

            StyleRange *sr  = &ranges[n_ranges++];
            sr->start       = rs;
            sr->end         = re;
            sr->has_fg      = a_fg != NULL;
            sr->has_bg      = a_bg != NULL;
            sr->underline   = a_ul != NULL;
            sr->strikethrough = a_st != NULL;

            if (a_fg) {
                PangoAttrColor *c = (PangoAttrColor *)a_fg;
                float alpha = base_color->alpha;
                if (a_fga)
                    alpha = ((PangoAttrInt *)a_fga)->value / 65535.f;
                sr->fg = (GdkRGBA){ c->color.red   / 65535.f,
                                    c->color.green / 65535.f,
                                    c->color.blue  / 65535.f, alpha };
            } else {
                sr->fg = *base_color;
            }

            if (a_bg) {
                PangoAttrColor *c = (PangoAttrColor *)a_bg;
                float alpha = 1.0f;
                if (a_bga)
                    alpha = ((PangoAttrInt *)a_bga)->value / 65535.f;
                sr->bg = (GdkRGBA){ c->color.red   / 65535.f,
                                    c->color.green / 65535.f,
                                    c->color.blue  / 65535.f, alpha };
            }
        } while (pango_attr_iterator_next(iter));
        pango_attr_iterator_destroy(iter);

        /* No styled ranges at all → single plain text node */
        if (n_ranges == 0) {
            PangoGlyphString gs = { run->n_glyphs, run->glyphs, NULL };
            GskRenderNode *node = gsk_text_node_new(
                run->font, &gs, base_color, &GRAPHENE_POINT_INIT(0, y));
            if (node)
                nodes[n++] = node;
            continue;
        }

        /* ── 2. Tag each glyph with its range index ────────── */
        int tags[run->n_glyphs];
        for (int i = 0; i < run->n_glyphs; i++) {
            tags[i] = -1;
            uint32_t bp = run->clusters[i];
            for (int c = 0; c < n_ranges; c++) {
                if ((int)bp >= ranges[c].start && (int)bp < ranges[c].end) {
                    tags[i] = c;
                    break;
                }
            }
        }

        /* ── 3. Font metrics for decorations (lazy, once) ──── */
        float ul_y = 0, ul_h = 0, st_y = 0, st_h = 0;
        float asc = 0, desc = 0;
        bool metrics_loaded = false;

        /* ── 4. Walk same-tagged glyph spans, emit nodes ───── */
        int i = 0, x = 0;
        while (i < run->n_glyphs) {
            int tag = tags[i];
            StyleRange *sr = (tag >= 0) ? &ranges[tag] : NULL;
            const GdkRGBA *fg = sr ? &sr->fg : base_color;
            int span_start = i;
            int span_x = x;
            while (i < run->n_glyphs && tags[i] == tag) {
                x += run->glyphs[i].geometry.width;
                i++;
            }
            float fx  = (float)span_x / PANGO_SCALE;
            float fw  = (float)(x - span_x) / PANGO_SCALE;

            /* Load font metrics the first time a decoration is needed */
            bool need_metrics = sr && (sr->has_bg || sr->underline || sr->strikethrough);
            if (need_metrics && !metrics_loaded) {
                PangoFontMetrics *m = pango_font_get_metrics(run->font, NULL);
                asc  = (float)pango_font_metrics_get_ascent(m)  / PANGO_SCALE;
                desc = (float)pango_font_metrics_get_descent(m) / PANGO_SCALE;
                ul_y = -(float)pango_font_metrics_get_underline_position(m)  / PANGO_SCALE;
                ul_h =  (float)pango_font_metrics_get_underline_thickness(m) / PANGO_SCALE;
                st_y = -(float)pango_font_metrics_get_strikethrough_position(m)  / PANGO_SCALE;
                st_h =  (float)pango_font_metrics_get_strikethrough_thickness(m) / PANGO_SCALE;
                pango_font_metrics_unref(m);
                metrics_loaded = true;
            }

            /* a) Background rect (behind text) */
            if (sr && sr->has_bg) {
                GskRenderNode *bg = gsk_color_node_new(
                    &sr->bg,
                    &GRAPHENE_RECT_INIT(fx, y - asc, fw, asc + desc));
                if (bg) nodes[n++] = bg;
            }

            /* b) Text glyphs */
            PangoGlyphString gs = { i - span_start, &run->glyphs[span_start], NULL };
            GskRenderNode *txt = gsk_text_node_new(
                run->font, &gs, fg,
                &GRAPHENE_POINT_INIT(fx, y));
            if (txt) nodes[n++] = txt;

            /* c) Underline rect */
            if (sr && sr->underline) {
                GskRenderNode *ul = gsk_color_node_new(
                    fg,
                    &GRAPHENE_RECT_INIT(fx, y + ul_y, fw, ul_h));
                if (ul) nodes[n++] = ul;
            }

            /* d) Strikethrough rect */
            if (sr && sr->strikethrough) {
                GskRenderNode *st = gsk_color_node_new(
                    fg,
                    &GRAPHENE_RECT_INIT(fx, y + st_y, fw, st_h));
                if (st) nodes[n++] = st;
            }
        }
    }

    self->node_cache     = g_realloc(nodes, (size_t)n * sizeof *nodes);
    self->n_cached_nodes = n;
    self->node_color     = *base_color;
    self->node_y         = y;
}

/* ==================================================================
 * Widget vfuncs
 * ================================================================== */

static void
bob_launcher_fast_label_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
    BobLauncherFastLabel *self = BOB_LAUNCHER_FAST_LABEL(widget);

    if (!self->shaped || self->shaped->n_runs == 0) {
        /* Fallback for labels that were never set_text'd */
        if (!self->layout) return;
        GdkRGBA color;
        gtk_widget_get_color(widget, &color);
        int bl = gtk_widget_get_baseline(widget);
        if (bl != -1) {
            float adj = bl - self->cached_baseline;
            gtk_snapshot_translate(snapshot, &GRAPHENE_POINT_INIT(0, adj));
        }
        gtk_snapshot_append_layout(snapshot, self->layout, &color);
        return;
    }

    GdkRGBA color;
    gtk_widget_get_color(widget, &color);
    int allocated_baseline = gtk_widget_get_baseline(widget);
    float y = (allocated_baseline != -1)
            ? (float)allocated_baseline
            : (float)self->cached_baseline;

    /* Rebuild the node cache only when something actually changed.
     * On a steady-state frame this is just N pointer appends.     */
    if (!self->node_cache ||
        !gdk_rgba_equal(&color, &self->node_color) ||
        self->node_y != y) {
        rebuild_node_cache(self, &color, y);
    }

    for (int i = 0; i < self->n_cached_nodes; i++)
        gtk_snapshot_append_node(snapshot, self->node_cache[i]);
}

static void
bob_launcher_fast_label_measure(GtkWidget *widget,
                                 GtkOrientation orientation,
                                 int for_size,
                                 int *minimum,
                                 int *natural,
                                 int *minimum_baseline,
                                 int *natural_baseline)
{
    BobLauncherFastLabel *self = BOB_LAUNCHER_FAST_LABEL(widget);

    wait_for_shaping(self);

    if (orientation == GTK_ORIENTATION_HORIZONTAL) {
        *minimum = *natural = self->cached_width;
        *minimum_baseline = *natural_baseline = -1;
    } else {
        *minimum = *natural = self->cached_height;
        *minimum_baseline = *natural_baseline = self->cached_baseline;
    }
}

static GtkSizeRequestMode
bob_launcher_fast_label_get_request_mode(GtkWidget *widget)
{
    return GTK_SIZE_REQUEST_CONSTANT_SIZE;
}

static void
bob_launcher_fast_label_finalize(GObject *object)
{
    BobLauncherFastLabel *self = BOB_LAUNCHER_FAST_LABEL(object);
    wait_for_shaping(self);
    drop_node_cache(self);
    hb_shaped_free(self->shaped);
    if (self->resolved_font) g_object_unref(self->resolved_font);
    free(self->pending_text);
    g_clear_object(&self->layout);
    G_OBJECT_CLASS(bob_launcher_fast_label_parent_class)->finalize(object);
}

static void
bob_launcher_fast_label_css_changed(GtkWidget *widget, GtkCssStyleChange *change)
{
    BobLauncherFastLabel *self = BOB_LAUNCHER_FAST_LABEL(widget);

    GTK_WIDGET_CLASS(bob_launcher_fast_label_parent_class)->css_changed(widget, change);
    wait_for_shaping(self);


    pango_layout_context_changed(self->layout);
    shape_sync(self);
    gtk_widget_queue_resize(GTK_WIDGET(self));
}

/* ==================================================================
 * Class / instance init
 * ================================================================== */

static void
bob_launcher_fast_label_class_init(BobLauncherFastLabelClass *klass, gpointer klass_data)
{
    bob_launcher_fast_label_parent_class = g_type_class_peek_parent(klass);

    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = bob_launcher_fast_label_finalize;

    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
    widget_class->snapshot = bob_launcher_fast_label_snapshot;
    widget_class->measure = bob_launcher_fast_label_measure;
    widget_class->get_request_mode = bob_launcher_fast_label_get_request_mode;
    widget_class->css_changed = bob_launcher_fast_label_css_changed;

    gtk_widget_class_set_css_name(widget_class, "label");
}

static void
bob_launcher_fast_label_instance_init(BobLauncherFastLabel *self, gpointer klass)
{
    self->layout = NULL;
    self->shaped = NULL;
    self->resolved_font = NULL;
    self->pending_text = NULL;
    self->cached_width = 0;
    self->cached_height = 0;
    self->cached_baseline = 0;
    self->node_cache     = NULL;
    self->n_cached_nodes = 0;
    self->node_color     = (GdkRGBA){0, 0, 0, 0};
    self->node_y         = 0;
    atomic_init(&self->shaping, 0);

    gtk_widget_set_overflow(GTK_WIDGET(self), GTK_OVERFLOW_VISIBLE);

    PangoContext *pango_ctx = gtk_widget_get_pango_context(GTK_WIDGET(self));
    self->layout = pango_layout_new(pango_ctx);
    pango_layout_set_single_paragraph_mode(self->layout, TRUE);
}

static GType
bob_launcher_fast_label_get_type_once(void)
{
    static const GTypeInfo info = {
        sizeof(BobLauncherFastLabelClass),
        NULL, NULL,
        (GClassInitFunc) bob_launcher_fast_label_class_init,
        NULL, NULL,
        sizeof(BobLauncherFastLabel),
        0,
        (GInstanceInitFunc) bob_launcher_fast_label_instance_init,
        NULL
    };
    return g_type_register_static(GTK_TYPE_WIDGET, "BobLauncherFastLabel", &info, 0);
}

GType
bob_launcher_fast_label_get_type(void)
{
    static gsize type_id = 0;
    if (g_once_init_enter(&type_id)) {
        GType id = bob_launcher_fast_label_get_type_once();
        g_once_init_leave(&type_id, id);
    }
    return type_id;
}

BobLauncherFastLabel*
bob_launcher_fast_label_new(void)
{
    return g_object_new(BOB_LAUNCHER_TYPE_FAST_LABEL, NULL);
}

/* ==================================================================
 * Public API
 * ================================================================== */

void
bob_launcher_fast_label_set_text(BobLauncherFastLabel *self,
                                  const char *text,
                                  PangoAttrList *attrs)
{
    wait_for_shaping(self);

    PangoAttrList *old_attrs = pango_layout_get_attributes(self->layout);
    bool attrs_changed = !pango_attr_list_equal(old_attrs, attrs);

    if (attrs_changed)
        pango_layout_set_attributes(self->layout, attrs);

    const char *old_text = pango_layout_get_text(self->layout);
    bool text_changed = strcmp(text, old_text) != 0;

    if (text_changed) {
        pango_layout_set_text(self->layout, text, -1);

        /* Capture text + font for the worker */
        free(self->pending_text);
        self->pending_text = strdup(text);

        PangoContext *pctx = pango_layout_get_context(self->layout);
        const PangoFontDescription *fd = pango_context_get_font_description(pctx);
        if (self->resolved_font)
            g_object_unref(self->resolved_font);
        self->resolved_font = hb_shaper_resolve_font(fd);

        fire_shaping(self);
        gtk_widget_queue_resize(GTK_WIDGET(self));
    } else if (attrs_changed) {
        drop_node_cache(self);   /* color spans changed → nodes stale */
        gtk_widget_queue_draw(GTK_WIDGET(self));
    }
}

void
bob_launcher_fast_label_invalidate(BobLauncherFastLabel *self)
{
    wait_for_shaping(self);
    pango_layout_context_changed(self->layout);
    shape_sync(self);
    gtk_widget_queue_resize(GTK_WIDGET(self));
}
