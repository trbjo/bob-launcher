#ifndef ICON_RENDERER_H
#define ICON_RENDERER_H

#include <gdk/gdk.h>
#include <stddef.h>

GdkTexture* icon_render_svg(const unsigned char *data, size_t len, int pixel_size);
GdkTexture* icon_render_png(const unsigned char *data, size_t len, int pixel_size);

#endif
