#include "launch-context.h"
#include "systemd-launcher.h"
#include "common-launcher.h"

#include <dbus/dbus.h>
#include <stdlib.h>
#include <string.h>

/* Launcher vtable for polymorphic dispatch */
typedef struct {
    bool (*launch_files)(void *self, GList *files, char **env);
    bool (*launch_uris)(void *self, GList *uris, char **env);
    bool (*launch_with_files)(void *self, GAppInfo *app, GList *files, const char *action, char **env);
    bool (*launch_with_uris)(void *self, GAppInfo *app, GList *uris, const char *action, char **env);
    bool (*launch_command)(void *self, const char *id, char **argv, char **env, bool blocking, bool needs_terminal);
    void (*destroy)(void *self);
} LauncherVTable;

/* VTable implementations for SystemdLauncher */
static bool systemd_vtable_launch_files(void *self, GList *files, char **env) {
    return systemd_launcher_launch_files((SystemdLauncher *)self, files, env);
}
static bool systemd_vtable_launch_uris(void *self, GList *uris, char **env) {
    return systemd_launcher_launch_uris((SystemdLauncher *)self, uris, env);
}
static bool systemd_vtable_launch_with_files(void *self, GAppInfo *app, GList *files, const char *action, char **env) {
    return systemd_launcher_launch_with_files((SystemdLauncher *)self, app, files, action, env);
}
static bool systemd_vtable_launch_with_uris(void *self, GAppInfo *app, GList *uris, const char *action, char **env) {
    return systemd_launcher_launch_with_uris((SystemdLauncher *)self, app, uris, action, env);
}
static bool systemd_vtable_launch_command(void *self, const char *id, char **argv, char **env, bool blocking, bool needs_terminal) {
    return systemd_launcher_launch_command((SystemdLauncher *)self, id, argv, env, blocking, needs_terminal);
}
static void systemd_vtable_destroy(void *self) {
    systemd_launcher_free((SystemdLauncher *)self);
}

static const LauncherVTable systemd_launcher_vtable = {
    .launch_files = systemd_vtable_launch_files,
    .launch_uris = systemd_vtable_launch_uris,
    .launch_with_files = systemd_vtable_launch_with_files,
    .launch_with_uris = systemd_vtable_launch_with_uris,
    .launch_command = systemd_vtable_launch_command,
    .destroy = systemd_vtable_destroy,
};

/* VTable implementations for CommonLauncher */
static bool common_vtable_launch_files(void *self, GList *files, char **env) {
    return common_launcher_launch_files((CommonLauncher *)self, files, env);
}
static bool common_vtable_launch_uris(void *self, GList *uris, char **env) {
    return common_launcher_launch_uris((CommonLauncher *)self, uris, env);
}
static bool common_vtable_launch_with_files(void *self, GAppInfo *app, GList *files, const char *action, char **env) {
    return common_launcher_launch_with_files((CommonLauncher *)self, app, files, action, env);
}
static bool common_vtable_launch_with_uris(void *self, GAppInfo *app, GList *uris, const char *action, char **env) {
    return common_launcher_launch_with_uris((CommonLauncher *)self, app, uris, action, env);
}
static bool common_vtable_launch_command(void *self, const char *id, char **argv, char **env, bool blocking, bool needs_terminal) {
    return common_launcher_launch_command((CommonLauncher *)self, id, argv, env, blocking, needs_terminal);
}
static void common_vtable_destroy(void *self) {
    common_launcher_free((CommonLauncher *)self);
}

static const LauncherVTable common_launcher_vtable = {
    .launch_files = common_vtable_launch_files,
    .launch_uris = common_vtable_launch_uris,
    .launch_with_files = common_vtable_launch_with_files,
    .launch_with_uris = common_vtable_launch_with_uris,
    .launch_command = common_vtable_launch_command,
    .destroy = common_vtable_destroy,
};

/* GObject type definition */
struct _BobLauncherLaunchContext {
    GObject parent_instance;

    void *launcher;
    const LauncherVTable *vtable;
};

struct _BobLauncherLaunchContextClass {
    GObjectClass parent_class;
};

static gpointer bob_launcher_bob_launch_context_parent_class = NULL;
static BobLauncherLaunchContext *instance = NULL;

/* Forward declarations */
static void bob_launcher_bob_launch_context_class_init(BobLauncherLaunchContextClass *klass, gpointer klass_data);
static void bob_launcher_bob_launch_context_instance_init(BobLauncherLaunchContext *self, gpointer klass);
static void bob_launcher_bob_launch_context_finalize(GObject *obj);

/* Type registration */
static GType
bob_launcher_bob_launch_context_get_type_once(void)
{
    static const GTypeInfo info = {
        sizeof(BobLauncherLaunchContextClass),
        NULL,
        NULL,
        (GClassInitFunc)bob_launcher_bob_launch_context_class_init,
        NULL,
        NULL,
        sizeof(BobLauncherLaunchContext),
        0,
        (GInstanceInitFunc)bob_launcher_bob_launch_context_instance_init,
        NULL
    };

    return g_type_register_static(G_TYPE_OBJECT, "BobLauncherLaunchContext", &info, 0);
}

GType
bob_launcher_bob_launch_context_get_type(void)
{
    static volatile gsize type_id__once = 0;

    if (g_once_init_enter(&type_id__once)) {
        GType type_id = bob_launcher_bob_launch_context_get_type_once();
        g_once_init_leave(&type_id__once, type_id);
    }

    return type_id__once;
}

static void
bob_launcher_bob_launch_context_class_init(BobLauncherLaunchContextClass *klass, gpointer klass_data)
{
    (void)klass_data;

    bob_launcher_bob_launch_context_parent_class = g_type_class_peek_parent(klass);

    G_OBJECT_CLASS(klass)->finalize = bob_launcher_bob_launch_context_finalize;
}

static void
bob_launcher_bob_launch_context_instance_init(BobLauncherLaunchContext *self, gpointer klass)
{
    (void)klass;

    const char *invocation_id = g_getenv("INVOCATION_ID");
    bool is_systemd_available = (invocation_id != NULL);

    if (is_systemd_available) {
        DBusError error;
        dbus_error_init(&error);

        DBusConnection *connection = dbus_bus_get(DBUS_BUS_SESSION, &error);

        if (dbus_error_is_set(&error)) {
            g_warning("Failed to initialize SystemdLauncher: %s", error.message);
            dbus_error_free(&error);

            self->launcher = common_launcher_new();
            self->vtable = &common_launcher_vtable;
            g_debug("Falling back to CommonLauncher");
        } else {
            SystemdLauncher *launcher = systemd_launcher_new(connection);
            dbus_connection_unref(connection);

            if (launcher != NULL) {
                self->launcher = launcher;
                self->vtable = &systemd_launcher_vtable;
                g_debug("Using SystemdLauncher");
            } else {
                self->launcher = common_launcher_new();
                self->vtable = &common_launcher_vtable;
                g_debug("Falling back to CommonLauncher");
            }
        }
    } else {
        self->launcher = common_launcher_new();
        self->vtable = &common_launcher_vtable;
        g_debug("Using CommonLauncher");
    }
}

static void
bob_launcher_bob_launch_context_finalize(GObject *obj)
{
    BobLauncherLaunchContext *self = BOB_LAUNCHER_LAUNCH_CONTEXT(obj);

    if (self->launcher != NULL && self->vtable != NULL) {
        self->vtable->destroy(self->launcher);
        self->launcher = NULL;
    }

    G_OBJECT_CLASS(bob_launcher_bob_launch_context_parent_class)->finalize(obj);
}

BobLauncherLaunchContext *
bob_launcher_bob_launch_context_get_instance(void)
{
    if (instance == NULL) {
        instance = g_object_new(BOB_LAUNCHER_TYPE_LAUNCH_CONTEXT, NULL);
    }
    return instance;
}

gboolean
bob_launcher_bob_launch_context_launch_command(BobLauncherLaunchContext *self,
                                            const gchar *identifier,
                                            gchar **argv,
                                            gboolean blocking,
                                            gboolean needs_terminal)
{
    g_return_val_if_fail(BOB_LAUNCHER_IS_LAUNCH_CONTEXT(self), FALSE);

    return self->vtable->launch_command(self->launcher, identifier, argv, NULL, blocking, needs_terminal);
}

gboolean
bob_launcher_bob_launch_context_launch_file(BobLauncherLaunchContext *self,
                                         GFile *file)
{
    g_return_val_if_fail(BOB_LAUNCHER_IS_LAUNCH_CONTEXT(self), FALSE);
    g_return_val_if_fail(G_IS_FILE(file), FALSE);

    GList *files = g_list_append(NULL, file);
    gboolean result = self->vtable->launch_files(self->launcher, files, NULL);
    g_list_free(files);

    return result;
}

gboolean
bob_launcher_bob_launch_context_launch_files_internal(BobLauncherLaunchContext *self,
                                                   GList *files,
                                                   gchar **env)
{
    g_return_val_if_fail(BOB_LAUNCHER_IS_LAUNCH_CONTEXT(self), FALSE);

    return self->vtable->launch_files(self->launcher, files, env);
}

gboolean
bob_launcher_bob_launch_context_launch_uris_internal(BobLauncherLaunchContext *self,
                                                  GList *uris,
                                                  gchar **env)
{
    g_return_val_if_fail(BOB_LAUNCHER_IS_LAUNCH_CONTEXT(self), FALSE);

    return self->vtable->launch_uris(self->launcher, uris, env);
}

gboolean
bob_launcher_bob_launch_context_launch_uri(BobLauncherLaunchContext *self,
                                        const gchar *uri)
{
    g_return_val_if_fail(BOB_LAUNCHER_IS_LAUNCH_CONTEXT(self), FALSE);
    g_return_val_if_fail(uri != NULL, FALSE);

    GList *uris = g_list_append(NULL, (gpointer)uri);
    gboolean result = self->vtable->launch_uris(self->launcher, uris, NULL);
    g_list_free(uris);

    return result;
}

gboolean
bob_launcher_bob_launch_context_launch_app(BobLauncherLaunchContext *self,
                                        GAppInfo *app_info,
                                        gboolean needs_terminal,
                                        const gchar *action)
{
    g_return_val_if_fail(BOB_LAUNCHER_IS_LAUNCH_CONTEXT(self), FALSE);
    g_return_val_if_fail(G_IS_APP_INFO(app_info), FALSE);

    (void)needs_terminal; /* Not used in original Vala code for this method */

    return self->vtable->launch_with_files(self->launcher, app_info, NULL, action, NULL);
}

gboolean
bob_launcher_bob_launch_context_launch_with_uri(BobLauncherLaunchContext *self,
                                             GAppInfo *app_info,
                                             const gchar *uri,
                                             const gchar *action)
{
    g_return_val_if_fail(BOB_LAUNCHER_IS_LAUNCH_CONTEXT(self), FALSE);
    g_return_val_if_fail(G_IS_APP_INFO(app_info), FALSE);
    g_return_val_if_fail(uri != NULL, FALSE);

    GList *uris = g_list_append(NULL, (gpointer)uri);
    gboolean result = self->vtable->launch_with_uris(self->launcher, app_info, uris, action, NULL);
    g_list_free(uris);

    return result;
}

gboolean
bob_launcher_bob_launch_context_launch_app_with_files(BobLauncherLaunchContext *self,
                                                   GAppInfo *app_info,
                                                   GList *files,
                                                   const gchar *action)
{
    g_return_val_if_fail(BOB_LAUNCHER_IS_LAUNCH_CONTEXT(self), FALSE);
    g_return_val_if_fail(G_IS_APP_INFO(app_info), FALSE);

    return self->vtable->launch_with_files(self->launcher, app_info, files, action, NULL);
}
