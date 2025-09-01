#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <limits.h>
#include <stddef.h>
#include "systemd_service_utils.h"
#include <stdarg.h>

int create_sync_socket(const char* sync_socket_name, size_t socket_name_len) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, sync_socket_name, socket_name_len);  // No & here!

    if (bind(sock, (struct sockaddr*)&addr,
            offsetof(struct sockaddr_un, sun_path) + socket_name_len) < 0) {  // Use actual length
        close(sock);
        return -1;
    }

    if (listen(sock, 1) < 0) {
        close(sock);
        return -1;
    }

    return sock;
}

int wait_for_ready_signal(int sync_sock) {
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;

    if (setsockopt(sync_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        return -1;
    }

    int client = accept(sync_sock, NULL, NULL);
    if (client < 0) {
        return -1;
    }

    char ready;
    ssize_t n = read(client, &ready, 1);
    close(client);

    return (n == 1) ? 0 : -1;
}

void signal_ready_if_needed(const char* sync_socket_name, size_t socket_name_len) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        return;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, sync_socket_name, socket_name_len);

    if (connect(sock, (struct sockaddr*)&addr,
            offsetof(struct sockaddr_un, sun_path) + socket_name_len) == 0) {
        char ready = 1;
        write(sock, &ready, 1);
    }

    close(sock);
}


int connect_abstract_blocking_with_timeout(const char *socket_name, size_t socket_name_len, int timeout_seconds) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = timeout_seconds;
    tv.tv_usec = 0;

    if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        close(sock);
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, socket_name, socket_name_len);

    int res = connect(sock, (struct sockaddr*)&addr,
            offsetof(struct sockaddr_un, sun_path) + socket_name_len);

    if (res < 0) {
        close(sock);
        return -1;
    }

    return sock;
}

/**
 * Creates a null-terminated array of environment variable strings.
 * Takes variable number of environment variable names, terminated by NULL.
 * Only includes variables that actually exist in the environment.
 *
 * Example usage:
 *   char **env = create_env_array("WAYLAND_DISPLAY", "XDG_ACTIVATION_TOKEN", "DISPLAY", NULL);
 *   start_as_systemd_service("my-service", argc, argv, env);
 *   free_env_array(env);
 *
 * Returns: Allocated array of strings, or NULL if no variables found
 */
char **create_env_array(const char *first_var, ...) {
    if (!first_var) return NULL;

    va_list args;
    const char *var_name;
    int count = 0;

    // First pass: count how many variables exist
    va_start(args, first_var);
    var_name = first_var;
    while (var_name) {
        if (getenv(var_name)) {
            count++;
        }
        var_name = va_arg(args, const char*);
    }
    va_end(args);

    if (count == 0) return NULL;

    // Allocate array (count + 1 for NULL terminator)
    char **env_array = malloc((count + 1) * sizeof(char*));
    if (!env_array) return NULL;

    // Second pass: populate the array
    va_start(args, first_var);
    var_name = first_var;
    int index = 0;

    while (var_name) {
        const char *var_value = getenv(var_name);
        if (var_value) {
            // Calculate needed size: "VAR=value\0"
            size_t needed = strlen(var_name) + strlen(var_value) + 2;
            env_array[index] = malloc(needed);
            if (!env_array[index]) {
                // Cleanup on failure
                for (int i = 0; i < index; i++) {
                    free(env_array[i]);
                }
                free(env_array);
                va_end(args);
                return NULL;
            }
            snprintf(env_array[index], needed, "%s=%s", var_name, var_value);
            index++;
        }
        var_name = va_arg(args, const char*);
    }
    va_end(args);

    // Null terminate
    env_array[index] = NULL;

    return env_array;
}

/**
 * Frees an environment array created by create_env_array()
 */
void free_env_array(char **env_array) {
    if (!env_array) return;

    for (int i = 0; env_array[i]; i++) {
        free(env_array[i]);
    }
    free(env_array);
}
