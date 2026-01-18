#include "systemd-launcher.h"
#include "access-appinfo-props.h"
#include "dbus-scope-monitor.h"
#include "atomic_helpers.h"
#include "systemd-scope.h"
#include "bob-launcher.h"

#include <gio/gdesktopappinfo.h>
#include <gdk/gdk.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>

#define LAUNCH_FILE_ATTRIBUTES G_FILE_ATTRIBUTE_STANDARD_TYPE "," G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE
extern char **environ;

struct SystemdLauncher {
    DBusConnection *connection;
    GSettings *settings;
    DBusScopeMonitor *scope_monitor;
    GHashTable *dbus_name_to_object_path;
    aligned_atomic_int *token;
    char *own_slice;
    char *systemd_slice;
    char *terminal_emulator;
};

/* Forward declarations */
static void on_dbus_event(DBusScopeEventType event_type, const char *dbus_name, const char *object_path, void *data);
static int launch_with_systemd_scope_internal(SystemdLauncher *self, const char *app_name, char **argv, char **env, bool blocking);
static char *create_scope_for_self(SystemdLauncher *self, const char *app_name);
static char *get_activation_token(GAppInfo *app_info);
static char **get_argv_for_app(SystemdLauncher *self, GAppInfo *app_info, const char *action, GList *files);
static char **get_argv_for_app_with_uris(SystemdLauncher *self, GAppInfo *app_info, const char *action, GList *uris);
static char **get_terminal_argv(SystemdLauncher *self, int *out_len);
static bool try_activate(SystemdLauncher *self, GAppInfo *app_info, const char *app_id, const char *object_path, GList *uris);
static int launch_with_files_internal(SystemdLauncher *self, GAppInfo *app_info, GList *files, const char *action, char **env, bool blocking);
static int launch_with_uris_internal(SystemdLauncher *self, GAppInfo *app_info, GList *uris, const char *action, char **env, bool blocking);
static int launch_wrapper_internal(SystemdLauncher *self, GAppInfo *app_info, char **argv, char **env, bool blocking);

static void
on_settings_changed(GSettings *settings, const char *key, gpointer user_data)
{
    SystemdLauncher *self = (SystemdLauncher *)user_data;

    if (g_strcmp0(key, "systemd-slice") == 0) {
        g_free(self->systemd_slice);
        self->systemd_slice = g_settings_get_string(settings, key);
    } else if (g_strcmp0(key, "terminal-emulator") == 0) {
        g_free(self->terminal_emulator);
        self->terminal_emulator = g_settings_get_string(settings, key);
    }
}

SystemdLauncher *
systemd_launcher_new(DBusConnection *connection)
{
    SystemdLauncher *self = malloc(sizeof(SystemdLauncher));
    if (self == NULL) {
        return NULL;
    }

    memset(self, 0, sizeof(SystemdLauncher));

    self->connection = connection;
    dbus_connection_ref(connection);

    self->token = aligned_atomic_int_new();
    self->dbus_name_to_object_path = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    int ret = get_caller_systemd_slice(&self->own_slice);
    if (ret != 0) {
        g_error("Failed to get slice: %s", strerror(-ret));
        systemd_launcher_free(self);
        return NULL;
    }

    self->settings = g_settings_new(BOB_LAUNCHER_APP_ID);
    self->systemd_slice = g_settings_get_string(self->settings, "systemd-slice");
    self->terminal_emulator = g_settings_get_string(self->settings, "terminal-emulator");

    g_signal_connect(self->settings, "changed", G_CALLBACK(on_settings_changed), self);

    self->scope_monitor = dbus_scope_monitor_new(on_dbus_event, self);
    dbus_scope_monitor_start(self->scope_monitor);

    return self;
}

void
systemd_launcher_free(SystemdLauncher *self)
{
    if (self == NULL) {
        return;
    }

    if (self->scope_monitor != NULL) {
        dbus_scope_monitor_stop(self->scope_monitor);
        dbus_scope_monitor_free(self->scope_monitor);
    }

    if (self->settings != NULL) {
        g_object_unref(self->settings);
    }

    if (self->connection != NULL) {
        dbus_connection_unref(self->connection);
    }

    if (self->dbus_name_to_object_path != NULL) {
        g_hash_table_destroy(self->dbus_name_to_object_path);
    }

    if (self->token != NULL) {
        aligned_atomic_int_free(self->token);
    }

    g_free(self->own_slice);
    g_free(self->systemd_slice);
    g_free(self->terminal_emulator);

    free(self);
}

static void
on_dbus_event(DBusScopeEventType event_type, const char *dbus_name, const char *object_path, void *data)
{
    SystemdLauncher *self = (SystemdLauncher *)data;

    spin_lock(self->token);

    if (event_type == DBUS_SCOPE_EVENT_CONNECTED) {
        g_hash_table_insert(self->dbus_name_to_object_path,
                           g_strdup(dbus_name),
                           g_strdup(object_path));
    } else if (event_type == DBUS_SCOPE_EVENT_DISCONNECTED) {
        g_hash_table_remove(self->dbus_name_to_object_path, dbus_name);
    }

    spin_unlock(self->token);
}

bool
systemd_launcher_launch_files(SystemdLauncher *self, GList *files, char **env)
{
    if (files == NULL || g_list_length(files) == 0) {
        return false;
    }

    /* Group files by their handler app */
    GHashTable *app_infos = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    /* Maps app name -> GPtrArray of URIs */
    GHashTable *app_to_uris = g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
                                                     (GDestroyNotify)g_ptr_array_unref);
    GHashTable *app_name_to_info = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, g_object_unref);

    for (GList *l = files; l != NULL; l = l->next) {
        GFile *file = G_FILE(l->data);
        char *original_uri = g_file_get_uri(file);
        char *path = g_file_get_path(file);

        /* Strip line:column suffix if present (e.g., file.txt:10:5) */
        if (path != NULL) {
            GRegex *regex = g_regex_new("^(.+?)(?::\\d+(?::\\d+)?)?$", 0, 0, NULL);
            GMatchInfo *match_info = NULL;

            if (g_regex_match(regex, path, 0, &match_info)) {
                char *clean_path = g_match_info_fetch(match_info, 1);
                g_free(path);
                path = clean_path;
            }

            g_match_info_free(match_info);
            g_regex_unref(regex);
        }

        GFile *clean_file = g_file_new_for_path(path);
        g_free(path);

        GError *error = NULL;
        GFileInfo *info = g_file_query_info(clean_file, LAUNCH_FILE_ATTRIBUTES,
                                            G_FILE_QUERY_INFO_NONE, NULL, &error);
        g_object_unref(clean_file);

        if (error != NULL) {
            g_warning("Could not query file info: %s", error->message);
            g_error_free(error);
            g_free(original_uri);
            continue;
        }

        const char *content_type = g_file_info_get_content_type(info);
        GAppInfo *app_info = g_app_info_get_default_for_type(content_type, FALSE);

        if (app_info == NULL) {
            /* Treat unknowns as text */
            app_info = g_app_info_get_default_for_type("text/plain", FALSE);
        }

        g_object_unref(info);

        if (app_info == NULL) {
            g_warning("no handlers detected for %s, cannot open", original_uri);
            g_free(original_uri);
            continue;
        }

        const char *name = g_app_info_get_name(app_info);
        GPtrArray *uri_array = g_hash_table_lookup(app_to_uris, name);

        if (uri_array == NULL) {
            uri_array = g_ptr_array_new_with_free_func(g_free);
            g_hash_table_insert(app_to_uris, (char *)name, uri_array);
            g_hash_table_insert(app_name_to_info, (char *)name, app_info);
        } else {
            g_object_unref(app_info);
        }

        g_ptr_array_add(uri_array, original_uri);
    }

    /* Launch each group */
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, app_to_uris);

    while (g_hash_table_iter_next(&iter, &key, &value)) {
        const char *name = (const char *)key;
        GPtrArray *uri_array = (GPtrArray *)value;
        GAppInfo *app_info = g_hash_table_lookup(app_name_to_info, name);

        GList *uri_list = NULL;
        for (guint i = 0; i < uri_array->len; i++) {
            uri_list = g_list_append(uri_list, g_ptr_array_index(uri_array, i));
        }

        launch_with_uris_internal(self, app_info, uri_list, NULL, env, false);
        g_list_free(uri_list);
    }

    g_hash_table_destroy(app_infos);
    g_hash_table_destroy(app_to_uris);
    g_hash_table_destroy(app_name_to_info);

    return true;
}

bool
systemd_launcher_launch_uris(SystemdLauncher *self, GList *uris, char **env)
{
    if (uris == NULL || g_list_length(uris) == 0) {
        return false;
    }

    /* Group URIs by their handler app */
    GHashTable *app_to_uris = g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
                                                     (GDestroyNotify)g_ptr_array_unref);
    GHashTable *app_name_to_info = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, g_object_unref);

    for (GList *l = uris; l != NULL; l = l->next) {
        const char *uri = (const char *)l->data;
        char *scheme = g_uri_parse_scheme(uri);

        if (scheme == NULL) {
            const char *colon = strchr(uri, ':');
            if (colon != NULL && colon > uri) {
                scheme = g_strndup(uri, colon - uri);
            }
        }

        if (scheme == NULL) {
            continue;
        }

        GAppInfo *app_info = NULL;

        if (g_strcmp0(scheme, "file") == 0) {
            GFile *file = g_file_new_for_uri(uri);
            GError *error = NULL;
            GFileInfo *info = g_file_query_info(file, LAUNCH_FILE_ATTRIBUTES,
                                                G_FILE_QUERY_INFO_NONE, NULL, &error);
            g_object_unref(file);

            if (error != NULL) {
                g_warning("Could not query file info: %s", error->message);
                g_error_free(error);
                g_free(scheme);
                continue;
            }

            const char *content_type = g_file_info_get_content_type(info);
            app_info = g_app_info_get_default_for_type(content_type, FALSE);

            if (app_info == NULL) {
                app_info = g_app_info_get_default_for_type("text/plain", FALSE);
            }

            g_object_unref(info);
        } else {
            app_info = g_app_info_get_default_for_uri_scheme(scheme);
        }

        g_free(scheme);

        if (app_info == NULL) {
            g_warning("no handlers detected for %s, cannot open", uri);
            continue;
        }

        const char *name = g_app_info_get_name(app_info);
        GPtrArray *uri_array = g_hash_table_lookup(app_to_uris, name);

        if (uri_array == NULL) {
            uri_array = g_ptr_array_new_with_free_func(g_free);
            g_hash_table_insert(app_to_uris, (char *)name, uri_array);
            g_hash_table_insert(app_name_to_info, (char *)name, app_info);
        } else {
            g_object_unref(app_info);
        }

        g_ptr_array_add(uri_array, g_strdup(uri));
    }

    /* Launch each group */
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, app_to_uris);

    while (g_hash_table_iter_next(&iter, &key, &value)) {
        const char *name = (const char *)key;
        GPtrArray *uri_array = (GPtrArray *)value;
        GAppInfo *app_info = g_hash_table_lookup(app_name_to_info, name);

        GList *uri_list = NULL;
        for (guint i = 0; i < uri_array->len; i++) {
            uri_list = g_list_append(uri_list, g_ptr_array_index(uri_array, i));
        }

        launch_with_uris_internal(self, app_info, uri_list, NULL, env, false);
        g_list_free(uri_list);
    }

    g_hash_table_destroy(app_to_uris);
    g_hash_table_destroy(app_name_to_info);

    return true;
}

bool
systemd_launcher_launch_with_files(SystemdLauncher *self, GAppInfo *app_info, GList *files, const char *action, char **env)
{
    return launch_with_files_internal(self, app_info, files, action, env, false) == 0;
}

bool
systemd_launcher_launch_with_uris(SystemdLauncher *self, GAppInfo *app_info, GList *uris, const char *action, char **env)
{
    return launch_with_uris_internal(self, app_info, uris, action, env, false) == 0;
}

bool
systemd_launcher_launch_command(SystemdLauncher *self, const char *identifier, char **argv, char **env, bool blocking, bool needs_terminal)
{
    if (argv == NULL || argv[0] == NULL) {
        return false;
    }

    int argc = 0;
    while (argv[argc] != NULL) {
        argc++;
    }

    char **final_argv;
    int final_argc;

    if (needs_terminal) {
        int term_len = 0;
        char **term_argv = get_terminal_argv(self, &term_len);

        final_argc = term_len + argc;
        final_argv = malloc(sizeof(char *) * (final_argc + 1));

        for (int i = 0; i < term_len; i++) {
            final_argv[i] = term_argv[i];
        }
        for (int i = 0; i < argc; i++) {
            final_argv[term_len + i] = argv[i];
        }
        final_argv[final_argc] = NULL;

        free(term_argv);
    } else {
        final_argv = argv;
        final_argc = argc;
    }

    int result = launch_with_systemd_scope_internal(self, identifier, final_argv, env, blocking);

    if (needs_terminal) {
        free(final_argv);
    }

    return result == 0;
}

static int
launch_with_files_internal(SystemdLauncher *self, GAppInfo *app_info, GList *files, const char *action, char **env, bool blocking)
{
    char **argv = get_argv_for_app(self, app_info, action, files);
    if (argv == NULL || argv[0] == NULL) {
        return -1;
    }

    char **final_env = env;
    char **allocated_env = NULL;
    bool has_activation_token = false;

    if (final_env == NULL) {
        final_env = g_get_environ();
        allocated_env = final_env;
    }

    /* Check if XDG_ACTIVATION_TOKEN is already set */
    for (int i = 0; final_env[i] != NULL; i++) {
        if (g_str_has_prefix(final_env[i], "XDG_ACTIVATION_TOKEN=")) {
            has_activation_token = true;
            break;
        }
    }

    if (!has_activation_token) {
        char *token = get_activation_token(app_info);
        if (token != NULL) {
            final_env = g_environ_setenv(g_strdupv(final_env), "XDG_ACTIVATION_TOKEN", token, TRUE);
            g_strfreev(allocated_env);
            allocated_env = final_env;
            g_free(token);
        }
    }

    int result = launch_wrapper_internal(self, app_info, argv, final_env, blocking);

    g_strfreev(argv);
    g_strfreev(allocated_env);

    return result;
}

static int
launch_with_uris_internal(SystemdLauncher *self, GAppInfo *app_info, GList *uris, const char *action, char **env, bool blocking)
{
    char **argv = get_argv_for_app_with_uris(self, app_info, action, uris);
    if (argv == NULL || argv[0] == NULL) {
        return -1;
    }

    int result = launch_wrapper_internal(self, app_info, argv, env, blocking);

    g_strfreev(argv);

    return result;
}

static int
launch_wrapper_internal(SystemdLauncher *self, GAppInfo *app_info, char **argv, char **env, bool blocking)
{
    const char *desktop_app_id = g_app_info_get_id(app_info);
    if (desktop_app_id == NULL) {
        g_error("app: %s, %s, does not have an id",
                g_app_info_get_name(app_info),
                g_app_info_get_display_name(app_info));
        return -1;
    }

    /* Remove .desktop suffix to get app_id */
    size_t len = strlen(desktop_app_id);
    char *app_id;
    if (len > 8 && g_str_has_suffix(desktop_app_id, ".desktop")) {
        app_id = g_strndup(desktop_app_id, len - 8);
    } else {
        app_id = g_strdup(desktop_app_id);
    }

    spin_lock(self->token);

    const char *object_path = g_hash_table_lookup(self->dbus_name_to_object_path, app_id);

    /* Count argv length */
    int argc = 0;
    while (argv[argc] != NULL) {
        argc++;
    }

    if (object_path == NULL || argc > 1) {
        spin_unlock(self->token);
        int result = launch_with_systemd_scope_internal(self, app_id, argv, env, blocking);
        g_free(app_id);
        return result;
    }

    char *object_path_copy = g_strdup(object_path);
    spin_unlock(self->token);

    bool success = try_activate(self, app_info, app_id, object_path_copy, NULL);

    g_free(app_id);
    g_free(object_path_copy);

    return success ? 0 : -1;
}

static bool
try_activate(SystemdLauncher *self, GAppInfo *app_info, const char *app_id, const char *object_path, GList *uris)
{
    DBusMessage *msg = NULL;
    DBusMessage *reply = NULL;
    DBusError error;
    bool success = false;

    dbus_error_init(&error);

    char *activation_token = get_activation_token(app_info);

    if (uris != NULL && g_list_length(uris) > 0) {
        msg = dbus_message_new_method_call(app_id, object_path,
                                           "org.freedesktop.Application", "Open");

        DBusMessageIter iter, array_iter, dict_iter;
        dbus_message_iter_init_append(msg, &iter);

        /* URI array */
        dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "s", &array_iter);
        for (GList *l = uris; l != NULL; l = l->next) {
            const char *uri = (const char *)l->data;
            dbus_message_iter_append_basic(&array_iter, DBUS_TYPE_STRING, &uri);
        }
        dbus_message_iter_close_container(&iter, &array_iter);

        /* Platform data dict */
        dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict_iter);

        if (activation_token != NULL) {
            DBusMessageIter entry_iter, variant_iter;

            dbus_message_iter_open_container(&dict_iter, DBUS_TYPE_DICT_ENTRY, NULL, &entry_iter);
            const char *key1 = "desktop-startup-id";
            dbus_message_iter_append_basic(&entry_iter, DBUS_TYPE_STRING, &key1);
            dbus_message_iter_open_container(&entry_iter, DBUS_TYPE_VARIANT, "s", &variant_iter);
            dbus_message_iter_append_basic(&variant_iter, DBUS_TYPE_STRING, &activation_token);
            dbus_message_iter_close_container(&entry_iter, &variant_iter);
            dbus_message_iter_close_container(&dict_iter, &entry_iter);

            dbus_message_iter_open_container(&dict_iter, DBUS_TYPE_DICT_ENTRY, NULL, &entry_iter);
            const char *key2 = "activation-token";
            dbus_message_iter_append_basic(&entry_iter, DBUS_TYPE_STRING, &key2);
            dbus_message_iter_open_container(&entry_iter, DBUS_TYPE_VARIANT, "s", &variant_iter);
            dbus_message_iter_append_basic(&variant_iter, DBUS_TYPE_STRING, &activation_token);
            dbus_message_iter_close_container(&entry_iter, &variant_iter);
            dbus_message_iter_close_container(&dict_iter, &entry_iter);
        }

        dbus_message_iter_close_container(&iter, &dict_iter);
    } else {
        msg = dbus_message_new_method_call(app_id, object_path,
                                           "org.freedesktop.Application", "Activate");

        DBusMessageIter iter, dict_iter;
        dbus_message_iter_init_append(msg, &iter);

        /* Platform data dict */
        dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict_iter);

        if (activation_token != NULL) {
            DBusMessageIter entry_iter, variant_iter;

            dbus_message_iter_open_container(&dict_iter, DBUS_TYPE_DICT_ENTRY, NULL, &entry_iter);
            const char *key1 = "desktop-startup-id";
            dbus_message_iter_append_basic(&entry_iter, DBUS_TYPE_STRING, &key1);
            dbus_message_iter_open_container(&entry_iter, DBUS_TYPE_VARIANT, "s", &variant_iter);
            dbus_message_iter_append_basic(&variant_iter, DBUS_TYPE_STRING, &activation_token);
            dbus_message_iter_close_container(&entry_iter, &variant_iter);
            dbus_message_iter_close_container(&dict_iter, &entry_iter);

            dbus_message_iter_open_container(&dict_iter, DBUS_TYPE_DICT_ENTRY, NULL, &entry_iter);
            const char *key2 = "activation-token";
            dbus_message_iter_append_basic(&entry_iter, DBUS_TYPE_STRING, &key2);
            dbus_message_iter_open_container(&entry_iter, DBUS_TYPE_VARIANT, "s", &variant_iter);
            dbus_message_iter_append_basic(&variant_iter, DBUS_TYPE_STRING, &activation_token);
            dbus_message_iter_close_container(&entry_iter, &variant_iter);
            dbus_message_iter_close_container(&dict_iter, &entry_iter);
        }

        dbus_message_iter_close_container(&iter, &dict_iter);
    }

    reply = dbus_connection_send_with_reply_and_block(self->connection, msg, 1000, &error);

    if (dbus_error_is_set(&error)) {
        g_warning("try_activate: FAILED - D-Bus call threw error: %s", error.message);
        dbus_error_free(&error);
    } else if (reply != NULL) {
        success = true;
    }

    g_free(activation_token);

    if (msg != NULL) {
        dbus_message_unref(msg);
    }
    if (reply != NULL) {
        dbus_message_unref(reply);
    }

    return success;
}

static int
launch_with_systemd_scope_internal(SystemdLauncher *self, const char *app_name, char **argv, char **_env, bool blocking)
{
    char **env = _env;
    char **allocated_env = NULL;

    if (env == NULL) {
        env = g_get_environ();
        allocated_env = env;
    }

    /* Duplicate argv for use in child process */
    int argc = 0;
    while (argv[argc] != NULL) {
        argc++;
    }

    char **argv_dup = malloc(sizeof(char *) * (argc + 1));
    for (int i = 0; i < argc; i++) {
        argv_dup[i] = strdup(argv[i]);
    }
    argv_dup[argc] = NULL;

    if (blocking) {
        pid_t child_pid = fork();

        if (child_pid < 0) {
            g_critical("Failed to fork child process");
            g_strfreev(allocated_env);
            g_strfreev(argv_dup);
            return -1;
        } else if (child_pid == 0) {
            /* Child process */
            setsid();

            char *scope_name = create_scope_for_self(self, app_name);
            if (scope_name == NULL) {
                g_warning("Failed to create systemd scope");
                _exit(1);
            }
            free(scope_name);

            /* Close all file descriptors */
            for (int fd = 0; fd < 1024; fd++) {
                close(fd);
            }

            int devnull = open("/dev/null", O_RDWR);
            if (devnull != 0) {
                dup2(devnull, 0);
            }
            dup2(devnull, 1);
            dup2(devnull, 2);
            if (devnull > 2) {
                close(devnull);
            }

            environ = env;
            execvp(argv_dup[0], argv_dup);
            _exit(127);
        }

        /* Parent process - wait for child */
        int status;
        waitpid(child_pid, &status, 0);

        g_strfreev(allocated_env);
        g_strfreev(argv_dup);

        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            return 128 + WTERMSIG(status);
        } else {
            return -1;
        }
    } else {
        /* Non-blocking: double fork */
        pid_t child_pid = fork();

        if (child_pid < 0) {
            g_critical("Failed to fork child process");
            g_strfreev(allocated_env);
            g_strfreev(argv_dup);
            return -1;
        } else if (child_pid == 0) {
            /* First child */
            pid_t grandchild_pid = fork();

            if (grandchild_pid < 0) {
                _exit(1);
            } else if (grandchild_pid == 0) {
                /* Grandchild */
                setsid();

                char *scope_name = create_scope_for_self(self, app_name);
                if (scope_name == NULL) {
                    g_warning("Failed to create systemd scope");
                    _exit(1);
                }
                free(scope_name);

                /* Close all file descriptors */
                for (int fd = 0; fd < 1024; fd++) {
                    close(fd);
                }

                int devnull = open("/dev/null", O_RDWR);
                if (devnull != 0) {
                    dup2(devnull, 0);
                }
                dup2(devnull, 1);
                dup2(devnull, 2);
                if (devnull > 2) {
                    close(devnull);
                }

                environ = env;
                execvp(argv_dup[0], argv_dup);
                g_warning("Failed to exec %s: %s", argv_dup[0], strerror(errno));
                _exit(127);
            } else {
                /* First child exits immediately */
                _exit(0);
            }
        }

        /* Parent waits for first child (which exits immediately) */
        int status;
        waitpid(child_pid, &status, 0);

        g_strfreev(allocated_env);
        g_strfreev(argv_dup);

        if (status != 0) {
            g_warning("Child process exited with status %d", status);
            return -1;
        }

        return 0;
    }
}

static char *
create_scope_for_self(SystemdLauncher *self, const char *app_name)
{
    GString *escaped_name = g_string_new(NULL);

    /* Escape app name: keep alphanumeric, ':', '_', '.'; escape others */
    const char *p = app_name;
    while (*p) {
        if (*p == ' ') {
            /* Skip spaces */
            p++;
            continue;
        }

        if (g_ascii_isalnum(*p) || *p == ':' || *p == '_' || *p == '.') {
            g_string_append_c(escaped_name, *p);
        } else {
            g_string_append_printf(escaped_name, "\\x%02x", (unsigned char)*p);
        }
        p++;
    }

    char *scope_name = g_strdup_printf("%s-%d.scope", escaped_name->str, (int)getpid());
    g_string_free(escaped_name, TRUE);

    const char *unit_slice;
    if (self->systemd_slice != NULL &&
        strlen(self->systemd_slice) > 6 &&
        g_str_has_suffix(self->systemd_slice, ".slice")) {
        unit_slice = self->systemd_slice;
    } else {
        unit_slice = self->own_slice;
    }

    char *job_path = create_systemd_scope_lowlevel(unit_slice, scope_name, getpid());

    if (job_path != NULL) {
        free(job_path);
        return scope_name;
    } else {
        g_warning("Failed to create systemd scope");
        g_free(scope_name);
        return NULL;
    }
}

static char **
get_terminal_argv(SystemdLauncher *self, int *out_len)
{
    if (self->terminal_emulator == NULL || self->terminal_emulator[0] == '\0') {
        if (out_len) *out_len = 0;
        return NULL;
    }

    char **parts = g_strsplit(self->terminal_emulator, " ", -1);
    int len = 0;
    while (parts[len] != NULL) {
        len++;
    }

    if (out_len) *out_len = len;
    return parts;
}

static char *
get_activation_token(GAppInfo *app_info)
{
    GdkDisplay *display = gdk_display_get_default();
    if (display == NULL) {
        return NULL;
    }

    GdkAppLaunchContext *context = gdk_display_get_app_launch_context(display);
    if (context == NULL) {
        return NULL;
    }

    char *token = g_app_launch_context_get_startup_notify_id(G_APP_LAUNCH_CONTEXT(context), app_info, NULL);
    g_object_unref(context);

    return token;
}

static char **
get_argv_for_app(SystemdLauncher *self, GAppInfo *app_info, const char *action, GList *files)
{
    if (!G_IS_DESKTOP_APP_INFO(app_info)) {
        g_warning("Cannot get commandline for non-DesktopAppInfo");
        return NULL;
    }

    GDesktopAppInfo *desktop_info = G_DESKTOP_APP_INFO(app_info);
    const char *commandline = NULL;
    char *action_commandline = NULL;

    if (action != NULL) {
        char *action_group = g_strdup_printf("Desktop Action %s", action);
        action_commandline = g_desktop_app_info_get_string_from_group(desktop_info, action_group, "Exec");
        g_free(action_group);
        commandline = action_commandline;
    } else {
        commandline = g_app_info_get_commandline(G_APP_INFO(desktop_info));
    }

    if (commandline == NULL) {
        g_free(action_commandline);
        return NULL;
    }

    GError *error = NULL;
    int argc;
    char **parts = NULL;

    if (!g_shell_parse_argv(commandline, &argc, &parts, &error)) {
        g_warning("Failed to parse command line: %s", error->message);
        g_error_free(error);
        g_free(action_commandline);
        return NULL;
    }

    bool needs_terminal = g_desktop_app_info_get_boolean(desktop_info, "Terminal");
    GPtrArray *argv = g_ptr_array_new();

    if (needs_terminal) {
        int term_len = 0;
        char **term_argv = get_terminal_argv(self, &term_len);
        for (int i = 0; i < term_len; i++) {
            g_ptr_array_add(argv, g_strdup(term_argv[i]));
        }
        g_strfreev(term_argv);
    }

    for (int i = 0; i < argc; i++) {
        const char *part = parts[i];

        if (g_strcmp0(part, "%f") == 0 || g_strcmp0(part, "%F") == 0) {
            if (files != NULL) {
                for (GList *l = files; l != NULL; l = l->next) {
                    GFile *file = G_FILE(l->data);
                    char *path = g_file_get_path(file);
                    if (path != NULL) {
                        g_ptr_array_add(argv, path);
                    } else {
                        g_ptr_array_add(argv, g_file_get_uri(file));
                    }
                }
            }
        } else if (g_strcmp0(part, "%u") == 0 || g_strcmp0(part, "%U") == 0) {
            if (files != NULL) {
                for (GList *l = files; l != NULL; l = l->next) {
                    GFile *file = G_FILE(l->data);
                    g_ptr_array_add(argv, g_file_get_uri(file));
                }
            }
        } else if (strchr(part, '%') == NULL) {
            g_ptr_array_add(argv, g_strdup(part));
        }
    }

    g_ptr_array_add(argv, NULL);

    g_strfreev(parts);
    g_free(action_commandline);

    return (char **)g_ptr_array_free(argv, FALSE);
}

static char **
get_argv_for_app_with_uris(SystemdLauncher *self, GAppInfo *app_info, const char *action, GList *uris)
{
    if (!G_IS_DESKTOP_APP_INFO(app_info)) {
        g_warning("Cannot get commandline for non-DesktopAppInfo");
        return NULL;
    }

    GDesktopAppInfo *desktop_info = G_DESKTOP_APP_INFO(app_info);
    const char *commandline = NULL;
    char *action_commandline = NULL;

    if (action != NULL) {
        char *action_group = g_strdup_printf("Desktop Action %s", action);
        action_commandline = g_desktop_app_info_get_string_from_group(desktop_info, action_group, "Exec");
        g_free(action_group);
        commandline = action_commandline;
    } else {
        commandline = g_app_info_get_commandline(G_APP_INFO(desktop_info));
    }

    if (commandline == NULL) {
        g_free(action_commandline);
        return NULL;
    }

    GError *error = NULL;
    int argc;
    char **parts = NULL;

    if (!g_shell_parse_argv(commandline, &argc, &parts, &error)) {
        g_warning("Failed to parse command line: %s", error->message);
        g_error_free(error);
        g_free(action_commandline);
        return NULL;
    }

    bool needs_terminal = g_desktop_app_info_get_boolean(desktop_info, "Terminal");
    GPtrArray *argv = g_ptr_array_new();

    if (needs_terminal) {
        int term_len = 0;
        char **term_argv = get_terminal_argv(self, &term_len);
        for (int i = 0; i < term_len; i++) {
            g_ptr_array_add(argv, g_strdup(term_argv[i]));
        }
        g_strfreev(term_argv);
    }

    for (int i = 0; i < argc; i++) {
        const char *part = parts[i];

        if (g_strcmp0(part, "%f") == 0 || g_strcmp0(part, "%F") == 0) {
            /* File placeholders - convert URI to file path */
            if (uris != NULL) {
                for (GList *l = uris; l != NULL; l = l->next) {
                    const char *uri = (const char *)l->data;
                    GFile *file = g_file_new_for_uri(uri);
                    char *path = g_file_get_path(file);
                    if (path != NULL) {
                        g_ptr_array_add(argv, path);
                    } else {
                        g_ptr_array_add(argv, g_strdup(uri));
                    }
                    g_object_unref(file);
                }
            }
        } else if (g_strcmp0(part, "%u") == 0 || g_strcmp0(part, "%U") == 0) {
            if (uris != NULL) {
                for (GList *l = uris; l != NULL; l = l->next) {
                    const char *uri = (const char *)l->data;
                    g_ptr_array_add(argv, g_strdup(uri));
                }
            }
        } else if (strchr(part, '%') == NULL) {
            g_ptr_array_add(argv, g_strdup(part));
        }
    }

    g_ptr_array_add(argv, NULL);

    g_strfreev(parts);
    g_free(action_commandline);

    return (char **)g_ptr_array_free(argv, FALSE);
}
