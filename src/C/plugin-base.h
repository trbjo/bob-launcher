#ifndef BOB_LAUNCHER_PLUGIN_BASE_H
#define BOB_LAUNCHER_PLUGIN_BASE_H

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

/* Forward declaration for Match interface */
#define BOB_LAUNCHER_TYPE_MATCH (bob_launcher_match_get_type())

#define BOB_LAUNCHER_TYPE_PLUGIN_BASE (bob_launcher_plugin_base_get_type())
#define BOB_LAUNCHER_PLUGIN_BASE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), BOB_LAUNCHER_TYPE_PLUGIN_BASE, BobLauncherPluginBase))
#define BOB_LAUNCHER_PLUGIN_BASE_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), BOB_LAUNCHER_TYPE_PLUGIN_BASE, BobLauncherPluginBaseClass))
#define BOB_LAUNCHER_IS_PLUGIN_BASE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), BOB_LAUNCHER_TYPE_PLUGIN_BASE))
#define BOB_LAUNCHER_IS_PLUGIN_BASE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), BOB_LAUNCHER_TYPE_PLUGIN_BASE))
#define BOB_LAUNCHER_PLUGIN_BASE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), BOB_LAUNCHER_TYPE_PLUGIN_BASE, BobLauncherPluginBaseClass))

typedef struct _BobLauncherPluginBase BobLauncherPluginBase;
typedef struct _BobLauncherPluginBaseClass BobLauncherPluginBaseClass;

struct _BobLauncherPluginBase {
    GObject parent_instance;

    atomic_int thread_id;
    atomic_int lock;
    double bonus;
    gboolean _is_enabled;
    gulong settings_handler_id;

    char *title;
    char *description;
    char *icon_name;
    char *_cached_string;
};

struct _BobLauncherPluginBaseClass {
    GObjectClass parent_class;

    /* Virtual methods from PluginBase */
    gboolean (*activate)(BobLauncherPluginBase *self);
    void (*deactivate)(BobLauncherPluginBase *self);
    void (*on_setting_changed)(BobLauncherPluginBase *self, const char *key, GVariant *value);
    gboolean (*handle_base_settings)(BobLauncherPluginBase *self, GSettings *settings, const char *key);
    void (*find_for_match)(BobLauncherPluginBase *self, void *match, void *rs);
    gulong (*initialize)(BobLauncherPluginBase *self, GSettings *settings);  // Add this

    /* Virtual methods from Match interface - these are abstract */
    const char* (*get_title)(BobLauncherPluginBase *self);
    const char* (*get_description)(BobLauncherPluginBase *self);
    const char* (*get_icon_name)(BobLauncherPluginBase *self);
    GtkWidget* (*get_tooltip)(BobLauncherPluginBase *self);
};

GType bob_launcher_plugin_base_get_type(void) G_GNUC_CONST;


typedef struct _BobLauncherMatch BobLauncherMatch;

typedef BobLauncherMatch* (*MatchFactory)(void* user_data);
MatchFactory bob_launcher_plugin_base_make_match (BobLauncherPluginBase* self, gpointer* result_target, GDestroyNotify* result_target_destroy_notify);

void bob_launcher_plugin_base_find_for_match(BobLauncherPluginBase *self, void *match, void *rs);
gboolean bob_launcher_plugin_base_activate(BobLauncherPluginBase *self);
void bob_launcher_plugin_base_deactivate(BobLauncherPluginBase *self);
void bob_launcher_plugin_base_on_setting_changed(BobLauncherPluginBase *self, const char *key, GVariant *value);
gboolean bob_launcher_plugin_base_handle_base_settings(BobLauncherPluginBase *self, GSettings *settings, const char *key);



gboolean bob_launcher_plugin_base_is_enabled(BobLauncherPluginBase *self);
gulong bob_launcher_plugin_base_initialize(BobLauncherPluginBase *self, GSettings *settings);
BobLauncherPluginBase* bob_launcher_plugin_base_construct(GType object_type);
void bob_launcher_plugin_base_shutdown(BobLauncherPluginBase *self);
const char* bob_launcher_plugin_base_get_mime_type(BobLauncherPluginBase *self);
const char* bob_launcher_plugin_base_to_string(BobLauncherPluginBase *self);

/* Match interface methods */
const char* bob_launcher_plugin_base_get_title(BobLauncherPluginBase *self);
const char* bob_launcher_plugin_base_get_description(BobLauncherPluginBase *self);
const char* bob_launcher_plugin_base_get_icon_name(BobLauncherPluginBase *self);
GtkWidget* bob_launcher_plugin_base_get_tooltip(BobLauncherPluginBase *self);

/* Property accessors */
double bob_launcher_plugin_base_get_bonus(BobLauncherPluginBase *self);
void bob_launcher_plugin_base_set_bonus(BobLauncherPluginBase *self, double value);

G_END_DECLS

#endif /* BOB_LAUNCHER_PLUGIN_BASE_H */
