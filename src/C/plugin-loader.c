#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dlfcn.h>
#include <dirent.h>
#include <sys/stat.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include "bob-launcher.h"

/* ============================================================================
 * Forward declarations for bob-launcher types and functions
 * ============================================================================ */

typedef struct _BobLauncherAppSettings BobLauncherAppSettings;
typedef struct _BobLauncherAppSettingsPlugins BobLauncherAppSettingsPlugins;

extern BobLauncherAppSettings *bob_launcher_app_settings_get_default(void);
extern BobLauncherAppSettingsPlugins *bob_launcher_app_settings_get_plugins(BobLauncherAppSettings *self);
extern gpointer bob_launcher_app_settings_plugins_ref(gpointer instance);
extern void bob_launcher_app_settings_plugins_unref(gpointer instance);
extern GHashTable *bob_launcher_app_settings_plugins_get_plugins(BobLauncherAppSettingsPlugins *self);
extern void bob_launcher_search_base_handle_base_settings(BobLauncherSearchBase *self, const char *key, GVariant *value);

/* ============================================================================
 * Type definitions
 * ============================================================================ */

typedef GType (*PluginInitFunc)(GTypeModule *type_module);

typedef struct {
    GTypeModule parent;
    char *path;
    void *handle;
    GType plugin_type;
} PluginModule;

typedef struct {
    GTypeModuleClass parent_class;
} PluginModuleClass;

/* ============================================================================
 * Static variables
 * ============================================================================ */

static char **plugin_dirs = NULL;
static int plugin_dirs_len = 0;
static GPtrArray *type_modules = NULL;
static GHashTable *handler_ids = NULL;
static BobLauncherAppSettingsPlugins *settings = NULL;

/* Public arrays */
GPtrArray *plugin_loader_search_providers = NULL;
GPtrArray *plugin_loader_default_search_providers = NULL;
GPtrArray *plugin_loader_loaded_plugins = NULL;
GPtrArray *plugin_loader_enabled_plugins = NULL;

static GType plugin_module_type = 0;

/* ============================================================================
 * Forward declarations
 * ============================================================================ */

static void on_plugin_enabled_changed(BobLauncherPluginBase *plugin);
static void on_plugin_enabled_changed_wrapper(GObject *obj, GParamSpec *param, gpointer user_data);
static void on_plugin_search_providers_changed_wrapper(GObject *obj, GParamSpec *param, gpointer user_data);
static void on_shard_count_changed(GObject *obj, GParamSpec *param, gpointer user_data);
static void on_provider_default_search_changed(GObject *obj, GParamSpec *param, gpointer user_data);
static void add_providers(BobLauncherPluginBase *plugin);
static void remove_providers(GPtrArray *providers);
static gulong initialize_plugin(BobLauncherPluginBase *plg, GSettings *plg_settings);
static int shard_comp(gconstpointer a, gconstpointer b);

/* ============================================================================
 * PluginModule GTypeModule subclass
 * ============================================================================ */

static gboolean plugin_module_load(GTypeModule *module) {
    PluginModule *self = (PluginModule *)module;
    return self->plugin_type != G_TYPE_INVALID;
}

static void plugin_module_unload(GTypeModule *module) {
    PluginModule *self = (PluginModule *)module;
    if (self->handle) {
        dlclose(self->handle);
        self->handle = NULL;
    }
}

static void plugin_module_finalize(GObject *obj) {
    PluginModule *self = (PluginModule *)obj;
    free(self->path);
    if (self->handle) {
        dlclose(self->handle);
    }
    G_OBJECT_CLASS(g_type_class_peek_parent(G_OBJECT_GET_CLASS(obj)))->finalize(obj);
}

static void plugin_module_class_init(PluginModuleClass *klass) {
    G_TYPE_MODULE_CLASS(klass)->load = plugin_module_load;
    G_TYPE_MODULE_CLASS(klass)->unload = plugin_module_unload;
    G_OBJECT_CLASS(klass)->finalize = plugin_module_finalize;
}

static void plugin_module_init(PluginModule *self) {
    self->path = NULL;
    self->handle = NULL;
    self->plugin_type = G_TYPE_INVALID;
}

static GType plugin_module_get_type(void) {
    if (plugin_module_type == 0) {
        static const GTypeInfo info = {
            sizeof(PluginModuleClass), NULL, NULL,
            (GClassInitFunc)plugin_module_class_init, NULL, NULL,
            sizeof(PluginModule), 0,
            (GInstanceInitFunc)plugin_module_init, NULL
        };
        plugin_module_type = g_type_register_static(G_TYPE_TYPE_MODULE, "PluginModule", &info, 0);
    }
    return plugin_module_type;
}

static PluginModule *plugin_module_new(const char *path) {
    PluginModule *self = g_object_new(plugin_module_get_type(), NULL);
    self->path = strdup(path);

    self->handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
    if (!self->handle) {
        g_warning("Plugin '%s' failed to load: %s", path, dlerror());
        return self;
    }

    PluginInitFunc init_func = (PluginInitFunc)dlsym(self->handle, "plugin_init");
    if (!init_func) {
        g_warning("Plugin '%s' doesn't have plugin_init symbol: %s", path, dlerror());
        return self;
    }

    self->plugin_type = init_func(G_TYPE_MODULE(self));
    if (self->plugin_type == G_TYPE_INVALID) {
        g_warning("Plugin '%s' returned invalid type", path);
    }

    return self;
}

/* ============================================================================
 * String utilities
 * ============================================================================ */

static char *str_replace(const char *str, const char *old, const char *new_str) {
    if (!str || !old || !new_str || !*old) return strdup(str ? str : "");

    size_t old_len = strlen(old);
    size_t new_len = strlen(new_str);

    int count = 0;
    const char *pos = str;
    while ((pos = strstr(pos, old))) { count++; pos += old_len; }
    if (count == 0) return strdup(str);

    char *result = malloc(strlen(str) + count * (new_len - old_len) + 1);
    char *dest = result;
    pos = str;
    const char *next;

    while ((next = strstr(pos, old))) {
        memcpy(dest, pos, next - pos);
        dest += next - pos;
        memcpy(dest, new_str, new_len);
        dest += new_len;
        pos = next + old_len;
    }
    strcpy(dest, pos);
    return result;
}

static int ends_with(const char *str, const char *suffix) {
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    return str_len >= suffix_len && strcmp(str + str_len - suffix_len, suffix) == 0;
}

/* ============================================================================
 * Settings helpers
 * ============================================================================ */

static char *get_plugin_summary(GSettings *plg_settings) {
    GSettingsSchema *schema = NULL;
    g_object_get(plg_settings, "settings-schema", &schema, NULL);

    if (schema) {
        GSettingsSchemaKey *key = g_settings_schema_get_key(schema, "enabled");
        if (key) {
            const char *summary = g_settings_schema_key_get_summary(key);
            char *result = strdup(summary ? summary : "No Title provided");
            g_settings_schema_key_unref(key);
            g_settings_schema_unref(schema);
            return result;
        }
        g_settings_schema_unref(schema);
    }
    return strdup("No Title provided");
}

static char *get_settings_description(GSettings *plg_settings) {
    GSettingsSchema *schema = NULL;
    g_object_get(plg_settings, "settings-schema", &schema, NULL);

    if (schema) {
        GSettingsSchemaKey *key = g_settings_schema_get_key(schema, "enabled");
        if (key) {
            const char *desc = g_settings_schema_key_get_description(key);
            char *result = strdup(desc ? desc : "No description provided");
            g_settings_schema_key_unref(key);
            g_settings_schema_unref(schema);
            return result;
        }
        g_settings_schema_unref(schema);
    }
    return strdup("No description provided");
}

/* ============================================================================
 * Plugin settings handling
 * ============================================================================ */

static void handle_enabled(BobLauncherPluginBase *plg, GSettings *plg_settings) {
    gboolean should_enable = g_settings_get_boolean(plg_settings, "enabled");
    gboolean current = bob_launcher_plugin_base_get_enabled(plg);

    if (current == should_enable) return;

    gboolean activated = should_enable && bob_launcher_plugin_base_activate(plg);

    if (!activated) {
        bob_launcher_plugin_base_deactivate(plg);
    }

    if (current != activated) {
        bob_launcher_plugin_base_set_enabled(plg, activated);
    }

    if (should_enable != activated) {
        g_settings_set_boolean(plg_settings, "enabled", activated);
    }
}

static void settings_changed_handler(GSettings *plg_settings, const char *key, gpointer user_data) {
    BobLauncherPluginBase *plg = BOB_LAUNCHER_PLUGIN_BASE(user_data);

    if (strcmp(key, "enabled") == 0) {
        handle_enabled(plg, plg_settings);
    } else {
        GVariant *value = g_settings_get_value(plg_settings, key);
        GPtrArray *providers = bob_launcher_plugin_base_get_search_providers(plg);

        for (guint i = 0; i < providers->len; i++) {
            BobLauncherSearchBase *sp = g_ptr_array_index(providers, i);
            bob_launcher_search_base_handle_base_settings(sp, key, value);
        }
        g_variant_unref(value);
    }
}

static void custom_settings_changed(GSettings *custom_settings, const char *key, gpointer user_data) {
    BobLauncherPluginBase *plg = BOB_LAUNCHER_PLUGIN_BASE(user_data);
    GVariant *value = g_settings_get_value(custom_settings, key);
    bob_launcher_plugin_base_on_setting_changed(plg, key, value);
    g_variant_unref(value);
}

static gulong initialize_plugin(BobLauncherPluginBase *plg, GSettings *plg_settings) {
    free(plg->title);
    plg->title = get_plugin_summary(plg_settings);
    free(plg->description);
    plg->description = get_settings_description(plg_settings);

    g_settings_bind(plg_settings, "bonus", G_OBJECT(plg), "bonus", G_SETTINGS_BIND_GET);

    char *schema_id = NULL;
    g_object_get(plg_settings, "schema-id", &schema_id, NULL);

    size_t len = strlen(schema_id) + strlen(".custom-settings") + 1;
    char *custom_schema_id = malloc(len);
    snprintf(custom_schema_id, len, "%s.custom-settings", schema_id);
    free(schema_id);

    GSettingsSchemaSource *schema_source = g_settings_schema_source_get_default();
    GSettingsSchema *custom_schema = g_settings_schema_source_lookup(schema_source, custom_schema_id, TRUE);

    if (custom_schema) {
        GSettings *custom_settings = g_settings_get_child(plg_settings, "custom-settings");

        GSettingsSchema *cs_schema = NULL;
        g_object_get(custom_settings, "settings-schema", &cs_schema, NULL);
        if (cs_schema) {
            char **keys = g_settings_schema_list_keys(cs_schema);
            for (int i = 0; keys[i]; i++) {
                char *plg_str = bob_launcher_plugin_base_to_string(plg);
                g_debug("key: %s, %s", keys[i], plg_str);
                free(plg_str);
                GVariant *value = g_settings_get_value(custom_settings, keys[i]);
                bob_launcher_plugin_base_on_setting_changed(plg, keys[i], value);
                g_variant_unref(value);
            }
            g_strfreev(keys);
            g_settings_schema_unref(cs_schema);
        }

        g_signal_connect(custom_settings, "changed", G_CALLBACK(custom_settings_changed), plg);
        g_settings_schema_unref(custom_schema);
    }

    free(custom_schema_id);

    handle_enabled(plg, plg_settings);
    return g_signal_connect(plg_settings, "changed", G_CALLBACK(settings_changed_handler), plg);
}

/* ============================================================================
 * Provider management
 * ============================================================================ */

static int shard_comp(gconstpointer a, gconstpointer b) {
    BobLauncherSearchBase *pa = *(BobLauncherSearchBase **)a;
    BobLauncherSearchBase *pb = *(BobLauncherSearchBase **)b;

    if (!pa && !pb) return 0;
    if (!pa) return -1;
    if (!pb) return 1;

    guint count_a = bob_launcher_search_base_get_shard_count(pa);
    guint count_b = bob_launcher_search_base_get_shard_count(pb);

    return (count_a > count_b) - (count_b > count_a);
}

static void on_shard_count_changed(GObject *obj, GParamSpec *param, gpointer user_data) {
    (void)obj; (void)param; (void)user_data;
    g_ptr_array_sort(plugin_loader_search_providers, shard_comp);
}

static void on_provider_default_search_changed(GObject *obj, GParamSpec *param, gpointer user_data) {
    (void)param; (void)user_data;
    BobLauncherSearchBase *provider = BOB_LAUNCHER_SEARCH_BASE(obj);

    if (bob_launcher_search_base_get_enabled_in_default_search(provider)) {
        if (!g_ptr_array_find(plugin_loader_default_search_providers, provider, NULL)) {
            g_ptr_array_add(plugin_loader_default_search_providers, g_object_ref(provider));
            char *title = bob_launcher_match_get_title(BOB_LAUNCHER_MATCH(provider));
            g_debug("Provider '%s' enabled in default search", title);
            free(title);
        }
    } else {
        g_ptr_array_remove(plugin_loader_default_search_providers, provider);
        char *title = bob_launcher_match_get_title(BOB_LAUNCHER_MATCH(provider));
        g_debug("Provider '%s' disabled from default search", title);
        free(title);
    }
}

static void add_providers(BobLauncherPluginBase *plugin) {
    GPtrArray *providers = bob_launcher_plugin_base_get_search_providers(plugin);

    for (guint i = 0; i < providers->len; i++) {
        BobLauncherSearchBase *provider = g_ptr_array_index(providers, i);
        g_ptr_array_add(plugin_loader_search_providers, g_object_ref(provider));

        g_signal_connect(provider, "notify::shard-count", G_CALLBACK(on_shard_count_changed), NULL);
        g_signal_connect(provider, "notify::enabled-in-default-search", G_CALLBACK(on_provider_default_search_changed), NULL);

        if (bob_launcher_search_base_get_enabled_in_default_search(provider)) {
            g_ptr_array_add(plugin_loader_default_search_providers, g_object_ref(provider));
        }

        char *title = bob_launcher_match_get_title(BOB_LAUNCHER_MATCH(provider));
        g_debug("Added search provider: %s", title);
        free(title);
    }

    g_ptr_array_sort(plugin_loader_search_providers, shard_comp);

    char *plugin_name = bob_launcher_plugin_base_to_string(plugin);
    size_t len = strlen(BOB_LAUNCHER_BOB_LAUNCHER_APP_ID) + strlen(".plugins.") + strlen(plugin_name) + 1;
    char *settings_id = malloc(len);
    snprintf(settings_id, len, "%s.plugins.%s", BOB_LAUNCHER_BOB_LAUNCHER_APP_ID, plugin_name);
    free(plugin_name);

    GSettings *plg_settings = g_settings_new(settings_id);

    GSettingsSchema *schema = NULL;
    g_object_get(plg_settings, "settings-schema", &schema, NULL);
    if (schema) {
        char **keys = g_settings_schema_list_keys(schema);
        for (int i = 0; keys[i]; i++) {
            GVariant *value = g_settings_get_value(plg_settings, keys[i]);
            for (guint j = 0; j < providers->len; j++) {
                BobLauncherSearchBase *sp = g_ptr_array_index(providers, j);
                bob_launcher_search_base_handle_base_settings(sp, keys[i], value);
            }
            g_variant_unref(value);
        }
        g_strfreev(keys);
        g_settings_schema_unref(schema);
    }

    g_object_unref(plg_settings);
    free(settings_id);
}

static void remove_providers(GPtrArray *providers) {
    for (guint i = 0; i < providers->len; i++) {
        BobLauncherSearchBase *provider = g_ptr_array_index(providers, i);

        g_ptr_array_remove(plugin_loader_default_search_providers, provider);
        g_signal_handlers_disconnect_by_func(provider, on_shard_count_changed, NULL);
        g_signal_handlers_disconnect_by_func(provider, on_provider_default_search_changed, NULL);
        g_ptr_array_remove(plugin_loader_search_providers, provider);

        char *title = bob_launcher_match_get_title(BOB_LAUNCHER_MATCH(provider));
        g_debug("Removed search provider: %s", title);
        free(title);
    }

    for (int i = (int)plugin_loader_search_providers->len - 1; i >= 0; i--) {
        if (g_ptr_array_index(plugin_loader_search_providers, i) == NULL) {
            g_ptr_array_remove_index(plugin_loader_search_providers, i);
            g_debug("plugin is null!");
        }
    }
}

/* ============================================================================
 * Plugin enable/disable handling
 * ============================================================================ */

static void on_plugin_enabled_changed(BobLauncherPluginBase *plugin) {
    if (bob_launcher_plugin_base_get_enabled(plugin)) {
        if (!g_ptr_array_find(plugin_loader_enabled_plugins, plugin, NULL)) {
            g_ptr_array_add(plugin_loader_enabled_plugins, plugin);
            char *title = bob_launcher_match_get_title(BOB_LAUNCHER_MATCH(plugin));
            g_debug("Plugin '%s' has been enabled", title);
            free(title);
            add_providers(plugin);
        }
    } else {
        g_ptr_array_remove(plugin_loader_enabled_plugins, plugin);
        char *title = bob_launcher_match_get_title(BOB_LAUNCHER_MATCH(plugin));
        g_debug("Plugin '%s' has been disabled", title);
        free(title);
        remove_providers(bob_launcher_plugin_base_get_search_providers(plugin));
    }
}

static void on_plugin_enabled_changed_wrapper(GObject *obj, GParamSpec *param, gpointer user_data) {
    (void)param; (void)user_data;
    on_plugin_enabled_changed(BOB_LAUNCHER_PLUGIN_BASE(obj));
}

static void on_plugin_search_providers_changed(BobLauncherPluginBase *plugin) {
    GPtrArray *new_providers = bob_launcher_plugin_base_get_search_providers(plugin);

    for (int i = (int)plugin_loader_search_providers->len - 1; i >= 0; i--) {
        BobLauncherSearchBase *provider = g_ptr_array_index(plugin_loader_search_providers, i);

        gboolean found = FALSE;
        for (guint j = 0; j < new_providers->len; j++) {
            if (g_ptr_array_index(new_providers, j) == provider) {
                found = TRUE;
                break;
            }
        }

        if (found) {
            g_ptr_array_remove(plugin_loader_search_providers, provider);
            g_ptr_array_remove(plugin_loader_default_search_providers, provider);
            g_signal_handlers_disconnect_by_func(provider, on_shard_count_changed, NULL);
            g_signal_handlers_disconnect_by_func(provider, on_provider_default_search_changed, NULL);
        }
    }

    if (!bob_launcher_plugin_base_get_enabled(plugin)) return;
    add_providers(plugin);
}

static void on_plugin_search_providers_changed_wrapper(GObject *obj, GParamSpec *param, gpointer user_data) {
    (void)param; (void)user_data;
    on_plugin_search_providers_changed(BOB_LAUNCHER_PLUGIN_BASE(obj));
}

/* ============================================================================
 * Public API
 * ============================================================================ */

void plugin_loader_initialize(void) {
    BobLauncherAppSettings *app_settings = bob_launcher_app_settings_get_default();
    settings = bob_launcher_app_settings_get_plugins(app_settings);
    bob_launcher_app_settings_plugins_ref(settings);

    const char *const *system_data_dirs = g_get_system_data_dirs();
    const char *user_data_dir = g_get_user_data_dir();

    int sys_dirs_count = 0;
    while (system_data_dirs[sys_dirs_count]) sys_dirs_count++;

    plugin_dirs_len = sys_dirs_count + 1;
    plugin_dirs = calloc(plugin_dirs_len + 1, sizeof(char *));

    char *user_lib = str_replace(user_data_dir, "share", "lib");
    plugin_dirs[0] = g_build_filename(user_lib, BOB_LAUNCHER_BOB_LAUNCHER_APP_ID, NULL);
    free(user_lib);

    for (int i = 0; i < sys_dirs_count; i++) {
        char *lib_path = str_replace(system_data_dirs[i], "share", "lib");
        plugin_dirs[i + 1] = g_build_filename(lib_path, BOB_LAUNCHER_BOB_LAUNCHER_APP_ID, NULL);
        free(lib_path);
    }

    type_modules = g_ptr_array_new_with_free_func(g_object_unref);
    plugin_loader_search_providers = g_ptr_array_new_with_free_func(g_object_unref);
    plugin_loader_default_search_providers = g_ptr_array_new_with_free_func(g_object_unref);
    plugin_loader_loaded_plugins = g_ptr_array_new_with_free_func(g_object_unref);
    plugin_loader_enabled_plugins = g_ptr_array_new();
    handler_ids = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, free);

    for (int d = 0; d < plugin_dirs_len; d++) {
        const char *plugin_dir = plugin_dirs[d];

        struct stat st;
        if (stat(plugin_dir, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        DIR *dir = opendir(plugin_dir);
        if (!dir) continue;

        struct dirent *entry;
        while ((entry = readdir(dir))) {
            if (!ends_with(entry->d_name, ".so")) continue;

            size_t path_len = strlen(plugin_dir) + strlen(entry->d_name) + 2;
            char *path = malloc(path_len);
            snprintf(path, path_len, "%s/%s", plugin_dir, entry->d_name);

            g_debug("Attempting to load plugin: %s", path);

            PluginModule *module = plugin_module_new(path);
            if (!g_type_module_use(G_TYPE_MODULE(module))) {
                g_warning("Failed to load plugin '%s'", entry->d_name);
                g_object_unref(module);
                free(path);
                continue;
            }

            g_ptr_array_add(type_modules, g_object_ref(module));

            GObject *obj = g_object_new(module->plugin_type, NULL);
            if (G_IS_INITIALLY_UNOWNED(obj)) g_object_ref_sink(obj);
            BobLauncherPluginBase *plugin = BOB_LAUNCHER_PLUGIN_BASE(obj);

            if (!plugin) {
                g_warning("Failed to create plugin instance");
                free(path);
                continue;
            }

            g_ptr_array_add(plugin_loader_loaded_plugins, g_object_ref(plugin));
            g_object_unref(plugin);
            g_object_unref(module);
            free(path);
        }

        closedir(dir);
    }

    GHashTable *plugins_hash = bob_launcher_app_settings_plugins_get_plugins(settings);

    for (guint i = 0; i < plugin_loader_loaded_plugins->len; i++) {
        BobLauncherPluginBase *plugin = g_ptr_array_index(plugin_loader_loaded_plugins, i);
        char *plugin_name = bob_launcher_plugin_base_to_string(plugin);
        g_debug("Plugin name resolved: %s", plugin_name);

        GSettings *plg_settings = g_hash_table_lookup(plugins_hash, plugin_name);
        if (plg_settings) {
            gulong handler_id = initialize_plugin(plugin, plg_settings);
            g_hash_table_insert(handler_ids, GUINT_TO_POINTER(handler_id), strdup(plugin_name));
        }
        free(plugin_name);
    }

    for (guint i = 0; i < plugin_loader_loaded_plugins->len; i++) {
        BobLauncherPluginBase *plugin = g_ptr_array_index(plugin_loader_loaded_plugins, i);
        g_signal_connect(plugin, "notify::enabled", G_CALLBACK(on_plugin_enabled_changed_wrapper), NULL);
        g_signal_connect(plugin, "notify::search-providers", G_CALLBACK(on_plugin_search_providers_changed_wrapper), NULL);
    }

    for (guint i = 0; i < plugin_loader_loaded_plugins->len; i++) {
        on_plugin_enabled_changed(g_ptr_array_index(plugin_loader_loaded_plugins, i));
    }

    g_ptr_array_sort(plugin_loader_search_providers, shard_comp);
    g_debug("Finished loading plugins. Loaded %u plugins total", plugin_loader_loaded_plugins->len);
}

static void disconnect_handler(gpointer key, gpointer value, gpointer user_data) {
    (void)user_data;
    GHashTable *plugins_hash = bob_launcher_app_settings_plugins_get_plugins(settings);
    GSettings *plg_settings = g_hash_table_lookup(plugins_hash, (const char *)value);
    if (plg_settings) g_signal_handler_disconnect(plg_settings, GPOINTER_TO_UINT(key));
}

void plugin_loader_shutdown(void) {
    g_debug("Shutting down plugin loader");

    g_hash_table_foreach(handler_ids, disconnect_handler, NULL);

    g_ptr_array_remove_range(plugin_loader_default_search_providers, 0, plugin_loader_default_search_providers->len);
    g_ptr_array_remove_range(plugin_loader_enabled_plugins, 0, plugin_loader_enabled_plugins->len);

    for (guint i = 0; i < plugin_loader_loaded_plugins->len; i++) {
        BobLauncherPluginBase *plugin = g_ptr_array_index(plugin_loader_loaded_plugins, i);
        g_signal_handlers_disconnect_by_func(plugin, on_plugin_enabled_changed_wrapper, NULL);
        g_signal_handlers_disconnect_by_func(plugin, on_plugin_search_providers_changed_wrapper, NULL);
    }

    g_ptr_array_remove_range(plugin_loader_search_providers, 0, plugin_loader_search_providers->len);

    for (guint i = 0; i < plugin_loader_loaded_plugins->len; i++) {
        BobLauncherPluginBase *plugin = g_ptr_array_index(plugin_loader_loaded_plugins, i);
        if (bob_launcher_plugin_base_get_enabled(plugin)) bob_launcher_plugin_base_deactivate(plugin);
    }

    for (guint i = 0; i < type_modules->len; i++) {
        PluginModule *module = g_ptr_array_index(type_modules, i);
        g_type_module_unuse(G_TYPE_MODULE(module));
    }

    g_ptr_array_set_free_func(type_modules, NULL);
    g_ptr_array_unref(type_modules);
    g_ptr_array_unref(plugin_loader_search_providers);
    g_ptr_array_unref(plugin_loader_default_search_providers);
    g_ptr_array_unref(plugin_loader_loaded_plugins);
    g_ptr_array_unref(plugin_loader_enabled_plugins);
    g_hash_table_unref(handler_ids);

    for (int i = 0; i < plugin_dirs_len; i++) free(plugin_dirs[i]);
    free(plugin_dirs);

    bob_launcher_app_settings_plugins_unref(settings);

    type_modules = NULL;
    plugin_loader_search_providers = NULL;
    plugin_loader_default_search_providers = NULL;
    plugin_loader_loaded_plugins = NULL;
    plugin_loader_enabled_plugins = NULL;
    handler_ids = NULL;
    plugin_dirs = NULL;
    settings = NULL;
}
