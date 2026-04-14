#pragma once

#include <gtk/gtk.h>
#include <cairo.h>

G_BEGIN_DECLS

typedef struct _GskSurfaceData GskSurfaceData;

GskSurfaceData *gsk_surface_data_new          (GType widget_type);
void            gsk_surface_data_reset        (GskSurfaceData *data);
void            gsk_surface_data_destroy      (GskSurfaceData *data);

gboolean        gsk_surface_data_snapshot     (GskSurfaceData  *data,
                                               GtkWidget       *widget,
                                               GskRenderNode  **out_node,
                                               cairo_region_t **out_region);

cairo_region_t *gsk_render_node_create_region (GskRenderNode *node,
                                               gint           width,
                                               gint           height);

G_END_DECLS
