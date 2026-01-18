#ifndef PLUGIN_LOADER_H
#define PLUGIN_LOADER_H

#include <glib.h>

extern GPtrArray *plugin_loader_search_providers;
extern GPtrArray *plugin_loader_default_search_providers;
extern GPtrArray *plugin_loader_loaded_plugins;
extern GPtrArray *plugin_loader_enabled_plugins;

extern void plugin_loader_initialize(void);
extern void plugin_loader_shutdown(void);

#endif
