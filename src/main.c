#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <stdint.h>
#include <libgen.h>

#include "liblauncher.h"
#include "systemd_service.h"
#include "systemd_service_utils.h"

static int flag_hidden = 0;
static enum { MODE_NONE, MODE_PLUGIN, MODE_OPEN } flag_mode = MODE_NONE;
static int flag_select_plugin = 0;
static char *flag_plugin_name = NULL;
static char plugin_args[4096] = {0};
static int plugin_args_len = 0;

static char **remaining_args = NULL;
static int remaining_argc = 0;

// Global structure to hold parsed environment variables
typedef struct {
    char **vars;
    int count;
    int capacity;
} EnvVarList;

static EnvVarList g_env_vars = {NULL, 0, 0};

typedef int (*run_launcher_func)(int);

static const char SOCKET_NAME[] = "\0io.github.trbjo.bob.launcher";
static const char SYNC_SOCKET_NAME[] = "\0io.github.trbjo.bob.launcher.sync";
static const char SERVICE_NAME[] = "io.github.trbjo.bob.launcher.service";

static void add_env_var(const char *var) {
    if (g_env_vars.count >= g_env_vars.capacity) {
        int new_capacity = g_env_vars.capacity == 0 ? 8 : g_env_vars.capacity * 2;
        g_env_vars.vars = realloc(g_env_vars.vars, new_capacity * sizeof(char*));
        g_env_vars.capacity = new_capacity;
    }

    const char *equals = strchr(var, '=');
    if (equals) {
        // Has an equals sign
        const char *value = equals + 1;
        if (*value == '$') {
            // Value starts with $, expand the environment variable
            const char *env_value = getenv(value + 1);
            if (env_value) {
                // Create new string with expanded value
                size_t key_len = equals - var;
                size_t new_len = key_len + 1 + strlen(env_value) + 1;
                char *expanded = malloc(new_len);
                memcpy(expanded, var, key_len);
                expanded[key_len] = '=';
                strcpy(expanded + key_len + 1, env_value);
                g_env_vars.vars[g_env_vars.count++] = expanded;
            } else {
                // Environment variable not found, use empty string
                size_t key_len = equals - var;
                size_t new_len = key_len + 2;  // key + '=' + '\0'
                char *expanded = malloc(new_len);
                memcpy(expanded, var, key_len);
                expanded[key_len] = '=';
                expanded[key_len + 1] = '\0';
                g_env_vars.vars[g_env_vars.count++] = expanded;
            }
        } else {
            // Use verbatim value
            g_env_vars.vars[g_env_vars.count++] = strdup(var);
        }
    } else {
        // No equals sign, look up the variable in current environment
        const char *env_value = getenv(var);
        if (env_value) {
            // Found the variable, create KEY=VALUE string
            size_t new_len = strlen(var) + 1 + strlen(env_value) + 1;
            char *expanded = malloc(new_len);
            sprintf(expanded, "%s=%s", var, env_value);
            g_env_vars.vars[g_env_vars.count++] = expanded;
        } else {
            // Variable not found in environment, store as KEY= (empty value)
            size_t new_len = strlen(var) + 2;  // key + '=' + '\0'
            char *expanded = malloc(new_len);
            sprintf(expanded, "%s=", var);
            g_env_vars.vars[g_env_vars.count++] = expanded;
        }
    }
}

static int parse_arguments(int argc, char **argv) {
    remaining_args = calloc(argc, sizeof(char *));
    if (!remaining_args) {
        fprintf(stderr, "Out of memory\n");
        return -1;
    }

    remaining_argc = 0;
    int flag_environment = 0;

    // First pass: find all flags
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--select-plugin") == 0) {
            flag_select_plugin = i;
        } else if (strcmp(argv[i], "--hidden") == 0) {
            flag_hidden = i;
        } else if (strcmp(argv[i], "--environment") == 0) {
            flag_environment = i;
        }
    }

    // Handle environment variables if --environment flag is present
    if (flag_environment) {
        int i = flag_environment + 1;
        while (i < argc && !(argv[i][0] == '-' && argv[i][1] == '-')) {
            add_env_var(argv[i]);
            i++;
        }
    }

    if (flag_select_plugin) {
        if (flag_select_plugin + 1 >= argc) {
            fprintf(stderr, "--select-plugin requires an argument\n");
            return -1;
        }

        flag_mode = MODE_PLUGIN;
        flag_plugin_name = argv[flag_select_plugin + 1];

        for (int j = flag_select_plugin + 2; j < argc; j++) {
            if (plugin_args_len > 0)
                plugin_args[plugin_args_len++] = ' ';

            int len = strlen(argv[j]);
            if (plugin_args_len + len >= sizeof(plugin_args) - 1) {
                fprintf(stderr, "Plugin arguments too long\n");
                return -1;
            }
            memcpy(plugin_args + plugin_args_len, argv[j], len);
            plugin_args_len += len;
        }
    }

    int args_end = flag_select_plugin ? flag_select_plugin : argc;

    // If --environment was found, also stop there if it comes before --select-plugin
    if (flag_environment && flag_environment < args_end) {
        args_end = flag_environment;
    }

    for (int i = 1; i < args_end; i++) {
        if (i != flag_hidden) {
            remaining_args[remaining_argc++] = argv[i];
            if (flag_mode == MODE_NONE)
                flag_mode = MODE_OPEN;
        }
    }

    return 0;
}

static void cleanup_env_vars(void) {
    for (int i = 0; i < g_env_vars.count; i++) {
        free(g_env_vars.vars[i]);
    }
    free(g_env_vars.vars);
}

static int create_listening_socket(void) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, SOCKET_NAME, sizeof(SOCKET_NAME));

    if (bind(sock, (struct sockaddr *)&addr,
             offsetof(struct sockaddr_un, sun_path) + sizeof(SOCKET_NAME) - 1) < 0) {
        close(sock);
        return -1;
    }

    if (listen(sock, 5) < 0) {
        close(sock);
        return -1;
    }

    return sock;
}

static int connect_abstract_blocking(void) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, SOCKET_NAME, sizeof(SOCKET_NAME));

    if (connect(sock, (struct sockaddr *)&addr,
                offsetof(struct sockaddr_un, sun_path) + sizeof(SOCKET_NAME) - 1) != 0) {
        close(sock);
        return -1;
    }

    return sock;
}

static int send_command_with_socket(int sock) {
    uint8_t buffer[8192];
    uint32_t pos = 4;

    const char *token = getenv("XDG_ACTIVATION_TOKEN");
    uint8_t token_len = token ? strlen(token) : 0;
    buffer[pos++] = token_len;
    if (token_len > 0) {
        memcpy(&buffer[pos], token, token_len);
        pos += token_len;
        buffer[pos++] = '\0';
    }

    if (flag_mode == MODE_PLUGIN) {
        buffer[pos++] = 'P';

        uint16_t len = strlen(flag_plugin_name);
        memcpy(&buffer[pos], &len, 2);
        pos += 2;
        memcpy(&buffer[pos], flag_plugin_name, len);
        pos += len;

        buffer[pos++] = '\0';
        if (plugin_args_len > 0) {
            memcpy(&buffer[pos], plugin_args, plugin_args_len);
            pos += plugin_args_len;
        }
        buffer[pos++] = '\0';

    } else if (flag_mode == MODE_OPEN) {
        buffer[pos++] = 'O';

        uint16_t count = remaining_argc;
        memcpy(&buffer[pos], &count, 2);
        pos += 2;

        for (int i = 0; i < remaining_argc; i++) {
            char resolved_path[PATH_MAX];
            char *path_to_use = remaining_args[i];

            if (realpath(path_to_use, resolved_path)) {
                path_to_use = resolved_path;
            }

            uint16_t len = strlen(path_to_use);

            if (pos + 2 + len + 1 >= sizeof(buffer)) {
                fprintf(stderr, "Command too long\n");
                return -1;
            }
            memcpy(&buffer[pos], &len, 2);
            pos += 2;
            memcpy(&buffer[pos], path_to_use, len);
            pos += len;
            buffer[pos++] = '\0';
        }

    } else {
        buffer[pos++] = flag_hidden ? 'H' : 'A';
    }

    uint32_t total_len = pos - 4;
    memcpy(buffer, &total_len, 4);

    ssize_t written = write(sock, buffer, pos);
    return (written == pos) ? 0 : -1;
}

int main(int argc, char **argv) {
    if (parse_arguments(argc, argv) < 0) {
        cleanup_env_vars();
        return 1;
    }

    char name_copy[256];
    strcpy(name_copy, argv[0]);
    int as_service = strcmp(basename(name_copy), SERVICE_NAME) == 0;

    int sock = connect_abstract_blocking();
    if (sock >= 0) {
        int result = send_command_with_socket(sock);
        close(sock);
        free(remaining_args);
        cleanup_env_vars();
        return result;
    }

    if (as_service) {
        int sync_sock = create_sync_socket(SYNC_SOCKET_NAME, sizeof(SYNC_SOCKET_NAME) - 1);
        if (sync_sock < 0) {
            fprintf(stderr, "Failed to create sync socket\n");
            cleanup_env_vars();
            return 1;
        }

        // Create environment array from parsed variables
        char **env = NULL;
        if (g_env_vars.count > 0) {
            // Convert our env var list to NULL-terminated array
            env = malloc((g_env_vars.count + 1) * sizeof(char*));
            for (int i = 0; i < g_env_vars.count; i++) {
                env[i] = g_env_vars.vars[i];
            }
            env[g_env_vars.count] = NULL;
        }

        if (start_as_systemd_service(SERVICE_NAME, 1, argv, env) < 0) {
            close(sync_sock);
            if (g_env_vars.count == 0) {
                free_env_array(env);
            } else {
                free(env);
            }
            fprintf(stderr, "Failed to start as systemd service\n");
            cleanup_env_vars();
            return 1;
        }

        if (g_env_vars.count == 0) {
            free_env_array(env);
        } else {
            free(env);
        }

        if (wait_for_ready_signal(sync_sock) < 0) {
            close(sync_sock);
            fprintf(stderr, "Service failed to signal ready\n");
            cleanup_env_vars();
            return 1;
        }
        close(sync_sock);

        sock = connect_abstract_blocking();
        if (sock >= 0) {
            flag_hidden = flag_hidden ? 0 : 1;
            // reverse the hide
            int result = send_command_with_socket(sock);
            close(sock);
            free(remaining_args);
            cleanup_env_vars();
            return result;
        }
        cleanup_env_vars();
        return 1;
    }

    int listen_sock = create_listening_socket();
    if (listen_sock < 0) {
        fprintf(stderr, "Failed to create launcher socket\n");
        cleanup_env_vars();
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(listen_sock);
        cleanup_env_vars();
        return 1;
    }

    if (pid == 0) {
        pid_t pid2 = fork();
        if (pid2 < 0) {
            perror("fork");
            _exit(1);
        }
        if (pid2 > 0) {
            _exit(0);
        }

        close(listen_sock);
        int sock = connect_abstract_blocking();
        if (sock < 0) {
            fprintf(stderr, "Failed to connect to launcher\n");
            return 1;
        }

        int result = send_command_with_socket(sock);
        close(sock);
        free(remaining_args);
        cleanup_env_vars();
        return result;
    }

    waitpid(pid, NULL, 0);

    void *handle;
    const char *debug_env = getenv("LAUNCHER_DEBUG");

    if (debug_env && debug_env[0] != '0') {
        const char *lib_path = "/tmp/libbob-launcher.so";
        int fd = open(lib_path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
        if (fd == -1 || write(fd, launcher_bin, launcher_bin_len) != launcher_bin_len) {
            perror("write");
            close(fd);
            close(listen_sock);
            cleanup_env_vars();
            return 1;
        }
        close(fd);
        handle = dlopen(lib_path, RTLD_NOW | RTLD_GLOBAL);
    } else {
        int fd = memfd_create("libbob-launcher", MFD_CLOEXEC);
        if (fd == -1 || write(fd, launcher_bin, launcher_bin_len) != launcher_bin_len) {
            perror("memfd/write");
            close(fd);
            close(listen_sock);
            cleanup_env_vars();
            return 1;
        }
        char fd_path[64];
        snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d", fd);
        handle = dlopen(fd_path, RTLD_NOW | RTLD_GLOBAL);
        close(fd);
    }

    if (!handle) {
        fprintf(stderr, "Failed to load libbob-launcher.so: %s\n", dlerror());
        close(listen_sock);
        cleanup_env_vars();
        return 1;
    }

    run_launcher_func run_launcher = (run_launcher_func)dlsym(handle, "run_launcher");
    if (!run_launcher) {
        fprintf(stderr, "Failed to find symbol run_launcher: %s\n", dlerror());
        dlclose(handle);
        close(listen_sock);
        cleanup_env_vars();
        return 1;
    }

    int exit_code = run_launcher(listen_sock);

    close(listen_sock);
    dlclose(handle);
    free(remaining_args);
    cleanup_env_vars();
    return exit_code;
}
