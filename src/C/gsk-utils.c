#include "gsk-utils.h"

struct _GskSurfaceData {
    GHashTable *cache;
    GtkWidgetClass *base_class;
    cairo_region_t *last_region; /* not owned — points into cache */
};

static gint
gsk_surface_data_pack(gint a, gint b)
{
    return (a << 16) | (b & 0xFFFF);
}

GskSurfaceData *
gsk_surface_data_new(GType widget_type)
{
    GskSurfaceData *data = g_new0(GskSurfaceData, 1);
    data->cache = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                        NULL, (GDestroyNotify)cairo_region_destroy);
    data->base_class = GTK_WIDGET_CLASS(
        g_type_class_peek_parent(g_type_class_peek(widget_type)));
    return data;
}

void
gsk_surface_data_reset(GskSurfaceData *data)
{
    data->last_region = NULL;
    g_hash_table_remove_all(data->cache);
}

void
gsk_surface_data_destroy(GskSurfaceData *data)
{
    if (data == NULL)
        return;
    g_hash_table_destroy(data->cache);
    g_free(data);
}

cairo_region_t *
gsk_render_node_create_region(GskRenderNode *node, gint width, gint height)
{
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_A8, width, height);
    cairo_t *cr = cairo_create(surf);
    gsk_render_node_draw(node, cr);
    cairo_destroy(cr);

    cairo_region_t *region = gdk_cairo_region_create_from_surface(surf);
    cairo_surface_destroy(surf);

    return region;
}

static const GskRoundedRect *
find_rounded_clip(GskRenderNode *node, float *offset_x, float *offset_y)
{
    if (node == NULL)
        return NULL;

    switch (gsk_render_node_get_node_type(node)) {
        case GSK_ROUNDED_CLIP_NODE:
            return gsk_rounded_clip_node_get_clip(node);
        case GSK_BORDER_NODE:
            return gsk_border_node_get_outline(node);
        case GSK_CONTAINER_NODE:
            for (guint i = 0; i < gsk_container_node_get_n_children(node); i++) {
                const GskRoundedRect *r =
                    find_rounded_clip(gsk_container_node_get_child(node, i), offset_x, offset_y);
                if (r != NULL) return r;
            }
            break;
        case GSK_TRANSFORM_NODE: {
            GskTransform *t = gsk_transform_node_get_transform(node);
            float dx, dy;
            if (gsk_transform_get_category(t) >= GSK_TRANSFORM_CATEGORY_2D_TRANSLATE) {
                gsk_transform_to_translate(t, &dx, &dy);
                *offset_x += dx;
                *offset_y += dy;
                const GskRoundedRect *r =
                    find_rounded_clip(gsk_transform_node_get_child(node), offset_x, offset_y);
                if (r != NULL) return r;
                *offset_x -= dx;
                *offset_y -= dy;
            }
            break;
        }
        case GSK_CLIP_NODE:
            return find_rounded_clip(gsk_clip_node_get_child(node), offset_x, offset_y);
        case GSK_OPACITY_NODE:
            return find_rounded_clip(gsk_opacity_node_get_child(node), offset_x, offset_y);
        case GSK_SHADOW_NODE:
            return find_rounded_clip(gsk_shadow_node_get_child(node), offset_x, offset_y);
        default:
            break;
    }

    return NULL;
}

static cairo_region_t *
gsk_render_node_create_shape_region(GskRenderNode *node, gint width, gint height)
{
    float offset_x = 0, offset_y = 0;
    const GskRoundedRect *clip = find_rounded_clip(node, &offset_x, &offset_y);

    if (clip != NULL) {
        GskRoundedRect positioned = *clip;
        positioned.bounds.origin.x += offset_x;
        positioned.bounds.origin.y += offset_y;

        GtkSnapshot *snap = gtk_snapshot_new();
        gtk_snapshot_push_rounded_clip(snap, &positioned);
        gtk_snapshot_append_color(snap, &(GdkRGBA){1, 1, 1, 1}, &positioned.bounds);
        gtk_snapshot_pop(snap);

        GskRenderNode *shape = gtk_snapshot_to_node(snap);
        g_object_unref(snap);

        if (shape != NULL) {
            cairo_region_t *region = gsk_render_node_create_region(shape, width, height);
            gsk_render_node_unref(shape);
            return region;
        }
    }

    return NULL;
}

gboolean
gsk_surface_data_snapshot(GskSurfaceData  *data,
                          GtkWidget       *widget,
                          GskRenderNode  **out_node,
                          cairo_region_t **out_region)
{
    GtkSnapshot *tmp = gtk_snapshot_new();
    data->base_class->snapshot(widget, tmp);
    *out_node = gtk_snapshot_to_node(tmp);
    g_object_unref(tmp);

    cairo_region_t *region = NULL;

    if (*out_node != NULL) {
        float offset_x = 0, offset_y = 0;
        if (find_rounded_clip(*out_node, &offset_x, &offset_y) != NULL) {
            gint width = gtk_widget_get_width(widget);
            gint height = gtk_widget_get_height(widget);
            gint key = gsk_surface_data_pack(width, height);

            region = g_hash_table_lookup(data->cache, GINT_TO_POINTER(key));

            if (region == NULL) {
                region = gsk_render_node_create_shape_region(*out_node, width, height);

                if (region == NULL || cairo_region_is_empty(region)) {
                    g_clear_pointer(&region, cairo_region_destroy);
                    region = NULL;
                } else {
                    g_hash_table_insert(data->cache, GINT_TO_POINTER(key), region);
                }
            }
        }
    }

    *out_region = region;

    gboolean different = (region != data->last_region &&
        (region == NULL || data->last_region == NULL ||
         !cairo_region_equal(region, data->last_region)));

    data->last_region = region;
    return different;
}
