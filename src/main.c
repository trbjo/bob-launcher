#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dbus/dbus.h>
#include <string.h>
#include <ctype.h>
#include "liblauncher.h"

typedef int (*run_launcher_func)(int argc, char **argv);

static char* path_to_uri(const char* path) {
    if (!path) return NULL;

    size_t len = strlen(path);
    char* escaped = malloc(len * 3 + 1);
    if (!escaped) return NULL;

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = path[i];
        if (isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped[j++] = c;
        } else if (c == ' ') {
            escaped[j++] = '%';
            escaped[j++] = '2';
            escaped[j++] = '0';
        } else {
            escaped[j++] = '%';
            escaped[j++] = "0123456789ABCDEF"[c >> 4];
            escaped[j++] = "0123456789ABCDEF"[c & 0xF];
        }
    }
    escaped[j] = '\0';

    char* uri = malloc(j + 8);
    if (!uri) {
        free(escaped);
        return NULL;
    }

    sprintf(uri, "file://%s", escaped);
    free(escaped);
    return uri;
}

static char* get_absolute_path(const char* path) {
    if (!path) return NULL;

    static char abs_path[PATH_MAX];
    if (realpath(path, abs_path) != NULL) {
        return abs_path;
    }
    return NULL;
}

static int is_uri(const char* path) {
    return (strncmp(path, "file://", 7) == 0 ||
            strncmp(path, "http://", 7) == 0 ||
            strncmp(path, "https://", 8) == 0);
}

static int file_exists(const char* path) {
    struct stat st;
    return (stat(path, &st) == 0);
}

static void append_file_paths(DBusMessageIter* array_iter, int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        char* file_path = argv[i];
        char* uri = NULL;

        if (!is_uri(file_path)) {
            char* abs_path = get_absolute_path(file_path);
            if (abs_path) {
                file_path = abs_path;
            }

            if (file_exists(file_path)) {
                uri = path_to_uri(file_path);
                if (uri) {
                    dbus_message_iter_append_basic(array_iter, DBUS_TYPE_STRING, &uri);
                    free(uri);
                    continue;
                }
            }
        }

        dbus_message_iter_append_basic(array_iter, DBUS_TYPE_STRING, &file_path);
    }
}

static void append_activation_token(DBusMessageIter* iter) {
    const char* token = getenv("XDG_ACTIVATION_TOKEN");
    if (token == NULL) {
        token = "";
    }
    dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &token);
}

static void append_empty_platform_data(DBusMessageIter* iter) {
    DBusMessageIter dict_iter;
    dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "{sv}", &dict_iter);
    dbus_message_iter_close_container(iter, &dict_iter);
}

int main(int argc, char **argv) {
    DBusError error;
    dbus_error_init(&error);

    DBusConnection *connection = dbus_bus_get(DBUS_BUS_SESSION, &error);

    if (connection && !dbus_error_is_set(&error)) {
        DBusMessage *message = NULL;

        if (argc >= 3 && strcmp(argv[1], "--select-plugin") == 0) {
            message = dbus_message_new_method_call(
                "io.github.trbjo.bob.launcher",
                "/io/github/trbjo/bob/launcher",
                "io.github.trbjo.bob.launcher",
                "SelectPlugin"
            );

            if (message) {
                const char *plugin_name = argv[2];
                const char *query_text = "";
                char query_buffer[1024] = "";
                if (argc > 3) {
                    for (int i = 3; i < argc; i++) {
                        if (i > 3) strcat(query_buffer, " ");
                        strcat(query_buffer, argv[i]);
                    }
                    query_text = query_buffer;
                }

                DBusMessageIter iter;
                dbus_message_iter_init_append(message, &iter);

                if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &plugin_name)) {
                    fprintf(stderr, "Out of memory!\n");
                    dbus_message_unref(message);
                    dbus_connection_unref(connection);
                    return 1;
                }

                if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &query_text)) {
                    fprintf(stderr, "Out of memory!\n");
                    dbus_message_unref(message);
                    dbus_connection_unref(connection);
                    return 1;
                }
            }
        } else if (argc >= 2) {
            message = dbus_message_new_method_call(
                "io.github.trbjo.bob.launcher",
                "/io/github/trbjo/bob/launcher",
                "org.gtk.Application",
                "Open"
            );

            if (message) {
                DBusMessageIter iter, array_iter;
                dbus_message_iter_init_append(message, &iter);

                dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
                                               DBUS_TYPE_STRING_AS_STRING, &array_iter);

                append_file_paths(&array_iter, argc, argv);

                dbus_message_iter_close_container(&iter, &array_iter);

                append_activation_token(&iter);
                append_empty_platform_data(&iter);
            }
        } else {
            message = dbus_message_new_method_call(
                "io.github.trbjo.bob.launcher",
                "/io/github/trbjo/bob/launcher",
                "org.gtk.Application",
                "Activate"
            );

            if (message) {
                // Create an empty array argument
                DBusMessageIter iter, array_iter;
                dbus_message_iter_init_append(message, &iter);

                dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
                                               "{sv}", &array_iter);
                dbus_message_iter_close_container(&iter, &array_iter);
            }
        }

        if (message) {
            DBusMessage *reply = dbus_connection_send_with_reply_and_block(
                connection, message, -1, &error);

            dbus_message_unref(message);

            if (reply) {
                dbus_message_unref(reply);
                dbus_connection_unref(connection);
                return 0;
            }
        }
    }

    if (dbus_error_is_set(&error)) {
        dbus_error_free(&error);
    }

    const char *debug_env = getenv("LAUNCHER_DEBUG");
    void *handle;

    if (debug_env != NULL) {
        // Debug mode: Write library to /tmp and load from there
        const char *lib_path = "/tmp/libbob-launcher.so";
        int fd = open(lib_path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
        if (fd == -1) {
            perror("open");
            return 1;
        }

        if (write(fd, launcher_bin, launcher_bin_len) != launcher_bin_len) {
            perror("write");
            close(fd);
            return 1;
        }
        close(fd);

        printf("Debug: Library written to %s\n", lib_path);
        handle = dlopen(lib_path, RTLD_NOW | RTLD_GLOBAL);
    } else {
        // Normal mode: Use memfd
        int fd = memfd_create("libbob-launcher", MFD_CLOEXEC);
        if (fd == -1) {
            perror("memfd_create");
            return 1;
        }

        if (write(fd, launcher_bin, launcher_bin_len) != launcher_bin_len) {
            perror("write");
            close(fd);
            return 1;
        }

        char fd_path[64];
        snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d", fd);
        handle = dlopen(fd_path, RTLD_NOW | RTLD_GLOBAL);
        close(fd);
    }

    if (!handle) {
        fprintf(stderr, "Failed to load libbob-launcher.so: %s\n", dlerror());
        return 1;
    }

    run_launcher_func run_launcher = (run_launcher_func)dlsym(handle, "run_launcher");
    if (!run_launcher) {
        fprintf(stderr, "Failed to find symbol run_launcher: %s\n", dlerror());
        dlclose(handle);
        return 1;
    }

    int exit_code = run_launcher(argc, argv);
    dlclose(handle);
    return exit_code;
}
