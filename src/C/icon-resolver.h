/* icon-theme-resolver.h */

#ifndef ICON_THEME_RESOLVER_H
#define ICON_THEME_RESOLVER_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    ICON_FORMAT_SVG,
    ICON_FORMAT_PNG,
} IconFormat;

typedef struct {
    char *path;
    bool is_resource;
    IconFormat format;
} IconLocation;

typedef struct IconResolver IconResolver;

IconResolver* icon_resolver_new(const char *theme_name,
                                const char **search_paths, int n_search_paths,
                                const char **resource_paths, int n_resource_paths);
void icon_resolver_destroy(IconResolver *resolver);

bool icon_resolver_lookup(IconResolver *resolver, const char *icon_name,
                          int size, int scale, IconLocation *out);

void icon_location_free(IconLocation *loc);

#endif
