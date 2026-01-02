#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <glib-unix.h>
#include <sys/socket.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#define SOCKET_ADDR_SYNC "io.github.trbjo.bob.launcher.sync"

typedef struct _BobLauncherLauncherWindow BobLauncherLauncherWindow;
typedef struct _BobLauncherBobLaunchContext BobLauncherBobLaunchContext;

extern int thread_pool_num_cores(void);
extern void thread_pool_init(int n);
extern void thread_pool_pin_caller(void);
extern void thread_pool_join_all(void);
extern void plugin_loader_initialize(void);
extern void plugin_loader_shutdown(void);
extern void state_initialize(void);
extern void icon_cache_service_initialize(void);
extern void keybindings_initialize(void);
extern void input_region_initialize(void);
extern void css_initialize(void);
extern void teardown(void);
extern void signal_ready_if_needed(uint8_t *socket_array, size_t len);
extern bool controller_select_plugin(const char *plugin, const char *query);

extern BobLauncherLauncherWindow *bob_launcher_launcher_window_new(void);
extern BobLauncherBobLaunchContext *bob_launcher_bob_launch_context_get_instance(void);
extern gboolean bob_launcher_bob_launch_context_launch_uris_internal(
    BobLauncherBobLaunchContext *self, GList *uris, char **env, int env_length);

static GMainLoop *main_loop = NULL;
BobLauncherLauncherWindow *bob_launcher_app_main_win = NULL;
static BobLauncherBobLaunchContext *launcher = NULL;
static int listen_fd = -1;
static guint listen_source_id = 0;

static void toggle_visibility(void) {
    gboolean visible = gtk_widget_get_visible(GTK_WIDGET(bob_launcher_app_main_win));
    gtk_widget_set_visible(GTK_WIDGET(bob_launcher_app_main_win), !visible);
}

static void open_uris(GList *uris, const char *token) {
    char **env = g_get_environ();

    if (token && *token) {
        env = g_environ_setenv(env, "XDG_ACTIVATION_TOKEN", token, TRUE);
    }

    int env_len = 0;
    for (char **p = env; *p; p++) env_len++;

    bob_launcher_bob_launch_context_launch_uris_internal(launcher, uris, env, env_len);
    g_strfreev(env);
}

static void select_plugin(const char *plugin, const char *query) {
    bool show = controller_select_plugin(plugin, query);
    gtk_widget_set_visible(GTK_WIDGET(bob_launcher_app_main_win), show);
}

static void handle_connection(int client_fd) {
    uint8_t len_buf[4];
    ssize_t n = read(client_fd, len_buf, 4);
    if (n != 4) {
        g_warning("Failed to read message length, got %zd bytes", n);
        return;
    }

    uint32_t msg_len = len_buf[0] | (len_buf[1] << 8) | (len_buf[2] << 16) | (len_buf[3] << 24);
    if (msg_len > 8192) {
        g_warning("Message too large: %u bytes", msg_len);
        return;
    }

    uint8_t *msg = malloc(msg_len);
    if (!msg) return;

    ssize_t total = 0;
    while (total < msg_len) {
        n = read(client_fd, msg + total, msg_len - total);
        if (n <= 0) {
            g_warning("Failed to read complete message");
            free(msg);
            return;
        }
        total += n;
    }

    uint32_t offset = 0;
    uint8_t token_len = msg[offset++];
    char *token = NULL;

    if (token_len > 0 && offset + token_len < msg_len) {
        token = g_strndup((char *)(msg + offset), token_len);
        g_setenv("XDG_ACTIVATION_TOKEN", token, TRUE);
        offset += token_len + 1;
    }

    if (offset >= msg_len) {
        g_free(token);
        free(msg);
        return;
    }

    uint8_t cmd = msg[offset++];

    switch (cmd) {
    case 'A':
        toggle_visibility();
        break;

    case 'H':
        break;

    case 'P': {
        if (offset + 2 > msg_len) {
            g_warning("Invalid plugin command");
            break;
        }

        uint16_t name_len = msg[offset] | (msg[offset + 1] << 8);
        offset += 2;

        if (offset + name_len > msg_len) {
            g_warning("Invalid plugin name length");
            break;
        }

        char *plugin_name = g_strndup((char *)(msg + offset), name_len);
        offset += name_len + 1;

        char *query = NULL;
        if (offset < msg_len) {
            query = g_strdup((char *)(msg + offset));
            if (query && *query == '\0') {
                g_free(query);
                query = NULL;
            }
        }

        select_plugin(plugin_name, query);
        g_free(plugin_name);
        g_free(query);
        break;
    }

    case 'O': {
        if (offset + 2 > msg_len) {
            g_warning("Invalid open command");
            break;
        }

        uint16_t count = msg[offset] | (msg[offset + 1] << 8);
        offset += 2;

        GList *uris = NULL;

        for (int i = 0; i < count && offset + 2 <= msg_len; i++) {
            uint16_t len = msg[offset] | (msg[offset + 1] << 8);
            offset += 2;

            if (offset + len > msg_len) break;

            char *path = g_strndup((char *)(msg + offset), len);
            offset += len + 1;

            if (g_file_test(path, G_FILE_TEST_EXISTS)) {
                GFile *f = g_file_new_for_path(path);
                char *uri = g_file_get_uri(f);
                g_object_unref(f);
                g_free(path);
                uris = g_list_append(uris, uri);
            } else {
                uris = g_list_append(uris, path);
            }
        }

        if (uris) {
            open_uris(uris, token);
            g_list_free_full(uris, g_free);
        }
        break;
    }

    default:
        g_warning("Unknown command: %c", cmd);
        break;
    }

    g_free(token);
    free(msg);
}

static gboolean on_incoming_connection(gint fd, GIOCondition condition, gpointer data) {
    (void)condition;
    (void)data;

    int client_fd = accept(fd, NULL, NULL);
    if (client_fd < 0) return G_SOURCE_CONTINUE;

    handle_connection(client_fd);
    close(client_fd);

    return G_SOURCE_CONTINUE;
}

static void quit(void) {
    if (main_loop) {
        g_main_loop_quit(main_loop);
    }
}

static gboolean on_sigint(gpointer data) {
    (void)data;
    quit();
    return G_SOURCE_REMOVE;
}

static gboolean on_sigterm(gpointer data) {
    (void)data;
    quit();
    return G_SOURCE_REMOVE;
}

static gboolean on_close_request(GtkWindow *window, gpointer data) {
    (void)window;
    (void)data;
    quit();
    return FALSE;
}

static void make_abstract_socket_name(const char *name, uint8_t *out, size_t *out_len) {
    size_t len = strlen(name);
    out[0] = '\0';
    memcpy(out + 1, name, len);
    *out_len = len + 1;
}

static void initialize(int fd) {
    int num_cores = thread_pool_num_cores();
    thread_pool_init(num_cores - 1);
    thread_pool_pin_caller();

    gtk_init();
    g_object_set(gtk_settings_get_default(), "gtk-enable-accels", FALSE, NULL);

    plugin_loader_initialize();
    state_initialize();
    icon_cache_service_initialize();
    keybindings_initialize();
    input_region_initialize();
    css_initialize();

    launcher = bob_launcher_bob_launch_context_get_instance();
    g_object_ref(launcher);

    bob_launcher_app_main_win = bob_launcher_launcher_window_new();
    g_object_ref_sink(bob_launcher_app_main_win);
    g_signal_connect(bob_launcher_app_main_win, "close-request", G_CALLBACK(on_close_request), NULL);

    listen_fd = fd;
    listen_source_id = g_unix_fd_add(listen_fd, G_IO_IN, on_incoming_connection, NULL);

    uint8_t socket_array[256];
    size_t socket_array_len;
    make_abstract_socket_name(SOCKET_ADDR_SYNC, socket_array, &socket_array_len);
    signal_ready_if_needed(socket_array, socket_array_len);
}

static void shutdown_app(void) {
    teardown();
    plugin_loader_shutdown();

    if (bob_launcher_app_main_win) {
        gtk_window_close(GTK_WINDOW(bob_launcher_app_main_win));
        gtk_window_destroy(GTK_WINDOW(bob_launcher_app_main_win));
        g_object_unref(bob_launcher_app_main_win);
        bob_launcher_app_main_win = NULL;
    }

    if (launcher) {
        g_object_unref(launcher);
        launcher = NULL;
    }

    if (listen_source_id) {
        g_source_remove(listen_source_id);
        listen_source_id = 0;
    }

    thread_pool_join_all();
}

int run_launcher(int socket_fd) {
    main_loop = g_main_loop_new(NULL, FALSE);

    g_unix_signal_add(SIGINT, on_sigint, NULL);
    g_unix_signal_add(SIGTERM, on_sigterm, NULL);

    initialize(socket_fd);

    g_main_loop_run(main_loop);

    shutdown_app();

    g_main_loop_unref(main_loop);
    main_loop = NULL;

    return 0;
}
