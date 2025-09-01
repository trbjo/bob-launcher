#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "thread-manager.h"
#include "plugin-base.h"

typedef struct _BobLauncherPluginBase BobLauncherPluginBase;
typedef struct _BobLauncherPluginBaseClass BobLauncherPluginBaseClass;

G_DEFINE_TYPE(BobLauncherPluginBase, bob_launcher_plugin_base, G_TYPE_OBJECT)

enum {
    PROP_0,
    PROP_BONUS,
    N_PROPS
};

static BobLauncherMatch*
___lambda__match_factory (gpointer self)
{
    return g_object_ref ((BobLauncherMatch*) self);
}

MatchFactory bob_launcher_plugin_base_make_match (BobLauncherPluginBase* self, gpointer* result_target, GDestroyNotify* result_target_destroy_notify)
{
    *result_target =  g_object_ref(self);
    *result_target_destroy_notify = g_object_unref;
    return ___lambda__match_factory;
}


static void acquire_lock(atomic_int *lock) {
    int expected = 0;
    while (!atomic_compare_exchange_weak(lock, &expected, 1)) {
        expected = 0;
        __builtin_ia32_pause();
    }
}

static void release_lock(atomic_int *lock) {
    atomic_store(lock, 0);
}

gboolean bob_launcher_plugin_base_is_enabled(BobLauncherPluginBase *self) {
    acquire_lock(&self->lock);
    gboolean result = self->thread_id == 0 && self->_is_enabled;
    release_lock(&self->lock);
    return result;
}

static void wait_for_completion(BobLauncherPluginBase *self) {
    acquire_lock(&self->lock);
    int tid = self->thread_id;
    release_lock(&self->lock);

    if (tid != 0) {
        thread_pool_join((uint64_t)tid);

        acquire_lock(&self->lock);
        self->thread_id = 0;
        release_lock(&self->lock);
    }
}

typedef struct {
    BobLauncherPluginBase *self;
    void (*task)(BobLauncherPluginBase *self);
} AsyncTaskData;

static void async_task_wrapper(void *data) {
    AsyncTaskData *task_data = (AsyncTaskData *)data;
    task_data->task(task_data->self);

    acquire_lock(&task_data->self->lock);
    task_data->self->thread_id = 0;
    release_lock(&task_data->self->lock);
}

static void async_task_destroy(void *data) {
    g_free(data);
}

static void run_async(BobLauncherPluginBase *self, void (*task)(BobLauncherPluginBase *self)) {
    wait_for_completion(self);

    AsyncTaskData *data = g_new(AsyncTaskData, 1);
    data->self = self;
    data->task = task;

    uint64_t tid = thread_pool_spawn_joinable(async_task_wrapper, data, async_task_destroy);

    acquire_lock(&self->lock);
    self->thread_id = (int)tid;
    release_lock(&self->lock);
}

static void handle_enabled_state(BobLauncherPluginBase *self, GSettings *settings) {
    bool enabled = g_settings_get_boolean(settings, "enabled");
    bool activated = enabled;

    if (enabled) {
        BobLauncherPluginBaseClass *klass = BOB_LAUNCHER_PLUGIN_BASE_GET_CLASS(self);
        if (klass->activate) {
            activated = klass->activate(self);
        }
    }

    if (enabled && !activated) {
        BobLauncherPluginBaseClass *klass = BOB_LAUNCHER_PLUGIN_BASE_GET_CLASS(self);
        if (klass->deactivate) {
            klass->deactivate(self);
        }
    }

    self->_is_enabled = activated;
    if (enabled != activated) {
        g_settings_set_boolean(settings, "enabled", activated);
    }
}

static void handle_specific_settings(BobLauncherPluginBase *self, GSettings *specific_settings) {
    GSettingsSchema *schema = NULL;
    g_object_get(specific_settings, "settings-schema", &schema, NULL);

    if (schema) {
        char **keys = g_settings_schema_list_keys(schema);
        for (int i = 0; keys[i] != NULL; i++) {
            BobLauncherPluginBaseClass *klass = BOB_LAUNCHER_PLUGIN_BASE_GET_CLASS(self);
            if (!klass->handle_base_settings || !klass->handle_base_settings(self, specific_settings, keys[i])) {
                GVariant *value = g_settings_get_value(specific_settings, keys[i]);
                if (klass->on_setting_changed) {
                    klass->on_setting_changed(self, keys[i], value);
                }
                g_variant_unref(value);
            }
        }
        g_strfreev(keys);
        g_settings_schema_unref(schema);
    }
}

typedef struct {
    GSettings *specific_settings;
    GSettings *parent_settings;
} InitialSettingsData;

static void process_initial_settings(BobLauncherPluginBase *self) {
    InitialSettingsData *data = g_object_get_data(G_OBJECT(self), "initial_settings_data");
    handle_specific_settings(self, data->specific_settings);
    handle_enabled_state(self, data->parent_settings);
}

typedef struct {
    GSettings *specific_settings;
    char *key;
} SettingChangeData;

static void process_setting_change(BobLauncherPluginBase *self) {
    SettingChangeData *data = g_object_get_data(G_OBJECT(self), "setting_change_data");
    BobLauncherPluginBaseClass *klass = BOB_LAUNCHER_PLUGIN_BASE_GET_CLASS(self);

    if (!klass->handle_base_settings || !klass->handle_base_settings(self, data->specific_settings, data->key)) {
        GVariant *value = g_settings_get_value(data->specific_settings, data->key);
        if (klass->on_setting_changed) {
            klass->on_setting_changed(self, data->key, value);
        }
        g_variant_unref(value);
    }
}

static void on_specific_setting_changed(GSettings *settings, const char *key, gpointer user_data) {
    BobLauncherPluginBase *self = BOB_LAUNCHER_PLUGIN_BASE(user_data);

    SettingChangeData *data = g_new(SettingChangeData, 1);
    data->specific_settings = g_object_ref(settings);
    data->key = g_strdup(key);

    g_object_set_data_full(G_OBJECT(self), "setting_change_data", data,
                           (GDestroyNotify)g_free);

    run_async(self, process_setting_change);
}

static void on_general_setting_changed(GSettings *settings, const char *key, gpointer user_data) {
    BobLauncherPluginBase *self = BOB_LAUNCHER_PLUGIN_BASE(user_data);
    g_object_set_data(G_OBJECT(self), "general_settings", settings);
    run_async(self, (void (*)(BobLauncherPluginBase *))handle_enabled_state);
}

static const char* get_summary(GSettings *settings) {
    GSettingsSchema *schema = NULL;
    g_object_get(settings, "settings-schema", &schema, NULL);

    if (schema) {
        GSettingsSchemaKey *key = g_settings_schema_get_key(schema, "enabled");
        if (key) {
            const char *summary = g_settings_schema_key_get_summary(key);
            g_settings_schema_key_unref(key);
            g_settings_schema_unref(schema);
            return summary ? summary : "No Title provided";
        }
        g_settings_schema_unref(schema);
    }
    return "No Title provided";
}

static const char* get_settings_description(GSettings *settings) {
    GSettingsSchema *schema = NULL;
    g_object_get(settings, "settings-schema", &schema, NULL);

    if (schema) {
        GSettingsSchemaKey *key = g_settings_schema_get_key(schema, "enabled");
        if (key) {
            const char *description = g_settings_schema_key_get_description(key);
            g_settings_schema_key_unref(key);
            g_settings_schema_unref(schema);
            return description ? description : "No description provided";
        }
        g_settings_schema_unref(schema);
    }
    return "No description provided";
}

void bob_launcher_plugin_base_find_for_match(BobLauncherPluginBase *self, void *match, void *rs) {
    g_return_if_fail(BOB_LAUNCHER_IS_PLUGIN_BASE(self));
    BobLauncherPluginBaseClass *klass = BOB_LAUNCHER_PLUGIN_BASE_GET_CLASS(self);
    if (klass->find_for_match) {
        klass->find_for_match(self, match, rs);
    }
}

gboolean bob_launcher_plugin_base_activate(BobLauncherPluginBase *self) {
    g_return_val_if_fail(BOB_LAUNCHER_IS_PLUGIN_BASE(self), TRUE);
    BobLauncherPluginBaseClass *klass = BOB_LAUNCHER_PLUGIN_BASE_GET_CLASS(self);
    if (klass->activate) {
        return klass->activate(self);
    }
    return TRUE; // Default implementation returns true
}

void bob_launcher_plugin_base_deactivate(BobLauncherPluginBase *self) {
    g_return_if_fail(BOB_LAUNCHER_IS_PLUGIN_BASE(self));
    BobLauncherPluginBaseClass *klass = BOB_LAUNCHER_PLUGIN_BASE_GET_CLASS(self);
    if (klass->deactivate) {
        klass->deactivate(self);
    }
}

void bob_launcher_plugin_base_on_setting_changed(BobLauncherPluginBase *self, const char *key, GVariant *value) {
    g_return_if_fail(BOB_LAUNCHER_IS_PLUGIN_BASE(self));
    BobLauncherPluginBaseClass *klass = BOB_LAUNCHER_PLUGIN_BASE_GET_CLASS(self);
    if (klass->on_setting_changed) {
        klass->on_setting_changed(self, key, value);
    } else {
        // Default implementation from Vala code
        g_error("Handling setting: '%s' for plugin: %s, but plugin does not override `on_setting_changed`",
                key, self->title);
    }
}

gboolean bob_launcher_plugin_base_handle_base_settings(BobLauncherPluginBase *self, GSettings *settings, const char *key) {
    g_return_val_if_fail(BOB_LAUNCHER_IS_PLUGIN_BASE(self), FALSE);
    BobLauncherPluginBaseClass *klass = BOB_LAUNCHER_PLUGIN_BASE_GET_CLASS(self);
    if (klass->handle_base_settings) {
        return klass->handle_base_settings(self, settings, key);
    }
    return FALSE; // Default implementation returns false
}



static gulong bob_launcher_plugin_base_initialize_default(BobLauncherPluginBase *self, GSettings *settings) {
    self->title = g_strdup(get_summary(settings));
    self->description = g_strdup(get_settings_description(settings));

    GSettingsSchemaSource *schema_source = g_settings_schema_source_get_default();
    // char *schema_id = g_strdup_printf("%s.settings", g_settings_get_schema_id(settings));
    GSettingsSchema *parent_schema = NULL;
    g_object_get(settings, "settings-schema", &parent_schema, NULL);
    const char *parent_schema_id = g_settings_schema_get_id(parent_schema);
    char *schema_id = g_strdup_printf("%s.settings", parent_schema_id);
    g_settings_schema_unref(parent_schema);


    GSettingsSchema *schema = g_settings_schema_source_lookup(schema_source, schema_id, TRUE);
    g_free(schema_id);

    if (schema) {
        g_settings_schema_unref(schema);
        GSettings *specific_settings = g_settings_get_child(settings, "settings");
        g_settings_bind(specific_settings, "bonus", self, "bonus", G_SETTINGS_BIND_GET);

        InitialSettingsData *init_data = g_new(InitialSettingsData, 1);
        init_data->specific_settings = g_object_ref(specific_settings);
        init_data->parent_settings = g_object_ref(settings);
        g_object_set_data_full(G_OBJECT(self), "initial_settings_data", init_data,
                              (GDestroyNotify)g_free);

        run_async(self, process_initial_settings);

        self->settings_handler_id = g_signal_connect(specific_settings, "changed",
                                                    G_CALLBACK(on_specific_setting_changed), self);

        g_object_unref(specific_settings);
    } else {
        handle_enabled_state(self, settings);
    }

    return g_signal_connect(settings, "changed", G_CALLBACK(on_general_setting_changed), self);
}

gulong bob_launcher_plugin_base_initialize(BobLauncherPluginBase *self, GSettings *settings) {
    g_return_val_if_fail(BOB_LAUNCHER_IS_PLUGIN_BASE(self), 0);
    BobLauncherPluginBaseClass *klass = BOB_LAUNCHER_PLUGIN_BASE_GET_CLASS(self);
    if (klass->initialize) {
        return klass->initialize(self, settings);
    }
    return 0;
}


void bob_launcher_plugin_base_shutdown(BobLauncherPluginBase *self) {
    if (bob_launcher_plugin_base_is_enabled(self)) {
        BobLauncherPluginBaseClass *klass = BOB_LAUNCHER_PLUGIN_BASE_GET_CLASS(self);
        if (klass->deactivate) {
            klass->deactivate(self);
        }
    }
    wait_for_completion(self);
}

const char* bob_launcher_plugin_base_get_mime_type(BobLauncherPluginBase *self) {
    return "application-x-executable";
}

const char* bob_launcher_plugin_base_to_string(BobLauncherPluginBase *self) {
    if (self->_cached_string == NULL) {
        const char *type_name = g_type_name(G_TYPE_FROM_INSTANCE(self));
        GString *result = g_string_new("");

        const char *start = strstr(type_name, "BobLauncher");
        if (start) {
            type_name = start + strlen("BobLauncher");
        }

        const char *plugin_pos = strstr(type_name, "Plugin");
        size_t len = plugin_pos ? (size_t)(plugin_pos - type_name) : strlen(type_name);

        for (size_t i = 0; i < len; i++) {
            if (i > 0 && g_ascii_isupper(type_name[i])) {
                g_string_append_c(result, '-');
            }
            g_string_append_c(result, g_ascii_tolower(type_name[i]));
        }

        self->_cached_string = g_string_free(result, FALSE);
    }
    return self->_cached_string;
}

// Match interface methods
const char* bob_launcher_plugin_base_get_title(BobLauncherPluginBase *self) {
    g_return_val_if_fail(BOB_LAUNCHER_IS_PLUGIN_BASE(self), NULL);
    BobLauncherPluginBaseClass *klass = BOB_LAUNCHER_PLUGIN_BASE_GET_CLASS(self);
    if (klass->get_title) {
        return klass->get_title(self);
    }
    return NULL;
}

const char* bob_launcher_plugin_base_get_description(BobLauncherPluginBase *self) {
    g_return_val_if_fail(BOB_LAUNCHER_IS_PLUGIN_BASE(self), NULL);
    BobLauncherPluginBaseClass *klass = BOB_LAUNCHER_PLUGIN_BASE_GET_CLASS(self);
    if (klass->get_description) {
        return klass->get_description(self);
    }
    return NULL;
}

const char* bob_launcher_plugin_base_get_icon_name(BobLauncherPluginBase *self) {
    g_return_val_if_fail(BOB_LAUNCHER_IS_PLUGIN_BASE(self), NULL);
    BobLauncherPluginBaseClass *klass = BOB_LAUNCHER_PLUGIN_BASE_GET_CLASS(self);
    if (klass->get_icon_name) {
        return klass->get_icon_name(self);
    }
    return NULL;
}

GtkWidget* bob_launcher_plugin_base_get_tooltip(BobLauncherPluginBase *self) {
    g_return_val_if_fail(BOB_LAUNCHER_IS_PLUGIN_BASE(self), NULL);
    BobLauncherPluginBaseClass *klass = BOB_LAUNCHER_PLUGIN_BASE_GET_CLASS(self);
    if (klass->get_tooltip) {
        return klass->get_tooltip(self);
    }
    return NULL;
}

// Property accessors
int16_t bob_launcher_plugin_base_get_bonus(BobLauncherPluginBase *self) {
    g_return_val_if_fail(BOB_LAUNCHER_IS_PLUGIN_BASE(self), 0.0);
    return self->bonus;
}

void bob_launcher_plugin_base_set_bonus(BobLauncherPluginBase *self, int16_t value) {
    g_return_if_fail(BOB_LAUNCHER_IS_PLUGIN_BASE(self));
    self->bonus = value;
    g_object_notify(G_OBJECT(self), "bonus");
}

static void bob_launcher_plugin_base_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    BobLauncherPluginBase *self = BOB_LAUNCHER_PLUGIN_BASE(object);

    switch (property_id) {
        case PROP_BONUS:
            g_value_set_double(value, self->bonus);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    }
}

static void bob_launcher_plugin_base_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
    BobLauncherPluginBase *self = BOB_LAUNCHER_PLUGIN_BASE(object);

    switch (property_id) {
        case PROP_BONUS:
            self->bonus = g_value_get_double(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    }
}

static void bob_launcher_plugin_base_finalize(GObject *object) {
    BobLauncherPluginBase *self = BOB_LAUNCHER_PLUGIN_BASE(object);

    g_free(self->title);
    g_free(self->description);
    g_free(self->icon_name);
    g_free(self->_cached_string);

    G_OBJECT_CLASS(bob_launcher_plugin_base_parent_class)->finalize(object);
}

static void bob_launcher_plugin_base_class_init(BobLauncherPluginBaseClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = bob_launcher_plugin_base_finalize;
    object_class->get_property = bob_launcher_plugin_base_get_property;
    object_class->set_property = bob_launcher_plugin_base_set_property;

    klass->initialize = bob_launcher_plugin_base_initialize_default;
    g_object_class_install_property(object_class, PROP_BONUS,
        g_param_spec_double("bonus", "Bonus", "Bonus score",
                           -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
                           G_PARAM_READWRITE));
}

static void bob_launcher_plugin_base_init(BobLauncherPluginBase *self) {
    atomic_init(&self->thread_id, 0);
    atomic_init(&self->lock, 0);
    self->_is_enabled = false;
    self->bonus = 0.0;
}

BobLauncherPluginBase* bob_launcher_plugin_base_construct(GType object_type) {
    BobLauncherPluginBase *self = (BobLauncherPluginBase*) g_object_new(object_type, NULL);
    return self;
}

