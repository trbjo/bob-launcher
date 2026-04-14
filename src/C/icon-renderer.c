#include "icon-renderer.h"
#include <librsvg/rsvg.h>
#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

GdkTexture* icon_render_svg(const unsigned char *data, size_t len, int pixel_size) {
    RsvgHandle *handle = rsvg_handle_new_from_data(data, len, NULL);
    if (!handle) return NULL;

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pixel_size, pixel_size);
    cairo_t *cr = cairo_create(surface);
    RsvgRectangle viewport = { 0, 0, pixel_size, pixel_size };
    rsvg_handle_render_document(handle, cr, &viewport, NULL);
    cairo_destroy(cr);
    g_object_unref(handle);

    cairo_surface_flush(surface);
    int w = cairo_image_surface_get_width(surface);
    int h = cairo_image_surface_get_height(surface);
    int stride = cairo_image_surface_get_stride(surface);

    GBytes *bytes = g_bytes_new_with_free_func(
        cairo_image_surface_get_data(surface),
        stride * h,
        (GDestroyNotify)cairo_surface_destroy,
        surface);
    GdkTexture *tex = gdk_memory_texture_new(w, h, GDK_MEMORY_B8G8R8A8_PREMULTIPLIED, bytes, stride);
    g_bytes_unref(bytes);

    return tex;
}

GdkTexture* icon_render_png(const unsigned char *data, size_t len, int pixel_size) {
    GInputStream *stream = g_memory_input_stream_new_from_data(data, len, NULL);
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_stream_at_scale(stream, pixel_size, pixel_size, TRUE, NULL, NULL);
    g_object_unref(stream);
    if (!pixbuf) return NULL;

    int w = gdk_pixbuf_get_width(pixbuf);
    int h = gdk_pixbuf_get_height(pixbuf);
    int stride = gdk_pixbuf_get_rowstride(pixbuf);
    gboolean alpha = gdk_pixbuf_get_has_alpha(pixbuf);

    GBytes *bytes = g_bytes_new_with_free_func(
        gdk_pixbuf_get_pixels(pixbuf),
        stride * h,
        g_object_unref,
        pixbuf);
    GdkTexture *tex = gdk_memory_texture_new(w, h,
        alpha ? GDK_MEMORY_R8G8B8A8 : GDK_MEMORY_R8G8B8, bytes, stride);
    g_bytes_unref(bytes);

    return tex;
}
