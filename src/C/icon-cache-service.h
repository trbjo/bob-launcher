#ifndef ICON_CACHE_SERVICE_H
#define ICON_CACHE_SERVICE_H

#include <glib.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

void icon_cache_service_initialize();

GdkPaintable* icon_cache_service_get_paintable_for_icon_name(const char *icon_name, int size, int scale);

const char* icon_cache_service_best_icon_name_for_mime_type(const char *content_type);

G_END_DECLS

#endif /* ICON_CACHE_SERVICE_H */
