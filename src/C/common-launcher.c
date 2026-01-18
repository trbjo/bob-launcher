#include "common-launcher.h"
#include <gdk/gdk.h>
#include <gio/gdesktopappinfo.h>
#include <stdlib.h>
#include <string.h>

/* External function from BobLauncher Utils */
extern void bob_launcher_utils_open_command_line(const char *command, const char *identifier, gboolean needs_terminal, void *working_dir);

struct CommonLauncher {
    int dummy; /* C requires at least one member */
};

static GAppLaunchContext *
get_launch_context(void)
{
    GdkDisplay *display = gdk_display_get_default();
    if (display != NULL) {
        return G_APP_LAUNCH_CONTEXT(gdk_display_get_app_launch_context(display));
    }
    return NULL;
}

CommonLauncher *
common_launcher_new(void)
{
    CommonLauncher *self = malloc(sizeof(CommonLauncher));
    if (self == NULL) {
        return NULL;
    }
    self->dummy = 0;
    return self;
}

void
common_launcher_free(CommonLauncher *self)
{
    if (self != NULL) {
        free(self);
    }
}

bool
common_launcher_launch_files(CommonLauncher *self, GList *files, char **env)
{
    (void)self;
    (void)env;

    if (files == NULL || g_list_length(files) == 0) {
        return false;
    }

    GFile *file = G_FILE(files->data);
    GError *error = NULL;

    GFileInfo *info = g_file_query_info(file,
                                        G_FILE_ATTRIBUTE_STANDARD_TYPE ","
                                        G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                                        G_FILE_QUERY_INFO_NONE,
                                        NULL,
                                        &error);
    if (error != NULL) {
        g_warning("Could not query file info: %s", error->message);
        g_error_free(error);
        return false;
    }

    GFileType file_type = g_file_info_get_file_type(info);

    if (file_type == G_FILE_TYPE_DIRECTORY) {
        g_object_unref(info);
        GList *uris = NULL;
        for (GList *l = files; l != NULL; l = l->next) {
            GFile *f = G_FILE(l->data);
            uris = g_list_append(uris, g_file_get_uri(f));
        }
        bool result = common_launcher_launch_uris(self, uris, env);
        g_list_free_full(uris, g_free);
        return result;
    }

    const char *content_type = g_file_info_get_content_type(info);
    GAppInfo *app_info = g_app_info_get_default_for_type(content_type, FALSE);
    g_object_unref(info);

    if (app_info != NULL) {
        bool result = common_launcher_launch_with_files(self, app_info, files, NULL, env);
        g_object_unref(app_info);
        return result;
    }

    return false;
}

bool
common_launcher_launch_uris(CommonLauncher *self, GList *uris, char **env)
{
    (void)self;
    (void)env;

    if (uris == NULL || g_list_length(uris) == 0) {
        return false;
    }

    GAppLaunchContext *context = get_launch_context();
    bool all_success = true;

    for (GList *l = uris; l != NULL; l = l->next) {
        const char *uri = (const char *)l->data;
        GError *error = NULL;

        if (!g_app_info_launch_default_for_uri(uri, context, &error)) {
            if (error != NULL) {
                g_warning("Could not launch URI %s: %s", uri, error->message);
                g_error_free(error);
            }
            all_success = false;
        }
    }

    if (context != NULL) {
        g_object_unref(context);
    }

    return all_success;
}

bool
common_launcher_launch_command(CommonLauncher *self, const char *identifier, char **argv, char **env, bool blocking, bool needs_terminal)
{
    (void)self;
    (void)env;
    (void)blocking;

    if (argv == NULL || argv[0] == NULL) {
        return false;
    }

    /* Join argv into a single command string */
    GString *command = g_string_new(NULL);
    for (int i = 0; argv[i] != NULL; i++) {
        if (i > 0) {
            g_string_append_c(command, ' ');
        }
        /* Simple quoting for arguments with spaces */
        if (strchr(argv[i], ' ') != NULL) {
            g_string_append_c(command, '"');
            g_string_append(command, argv[i]);
            g_string_append_c(command, '"');
        } else {
            g_string_append(command, argv[i]);
        }
    }

    bob_launcher_utils_open_command_line(command->str, identifier, needs_terminal, NULL);
    g_string_free(command, TRUE);

    return true;
}

bool
common_launcher_launch_with_files(CommonLauncher *self, GAppInfo *app_info, GList *files, const char *action, char **env)
{
    (void)self;
    (void)action;
    (void)env;

    GAppLaunchContext *context = get_launch_context();
    GError *error = NULL;

    bool result = g_app_info_launch(app_info, files, context, &error);

    if (error != NULL) {
        g_warning("Could not launch files with handler: %s", error->message);
        g_error_free(error);
    }

    if (context != NULL) {
        g_object_unref(context);
    }

    return result;
}

bool
common_launcher_launch_with_uris(CommonLauncher *self, GAppInfo *app_info, GList *uris, const char *action, char **env)
{
    (void)self;
    (void)action;
    (void)env;

    GAppLaunchContext *context = get_launch_context();
    GError *error = NULL;

    bool result = g_app_info_launch_uris(app_info, uris, context, &error);

    if (error != NULL) {
        g_warning("Could not launch URIs with handler: %s", error->message);
        g_error_free(error);
    }

    if (context != NULL) {
        g_object_unref(context);
    }

    return result;
}
