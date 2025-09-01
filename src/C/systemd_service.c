#define _GNU_SOURCE
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
#include <dbus/dbus.h>
#include "systemd_service.h"
#include <stdarg.h>

int start_as_systemd_service(const char* service_name, int argc, char **argv, char **environment) {
    DBusError err;
    DBusConnection *conn;
    DBusMessage *msg;
    DBusMessage *reply;
    DBusMessageIter args, props, prop, variant, exec_array, exec_struct, argv_array;
    DBusMessageIter env_prop, env_variant, env_array;
    DBusMessageIter aux;

    dbus_error_init(&err);

    conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "Connection Error (%s)\n", err.message);
        dbus_error_free(&err);
        return -1;
    }
    if (conn == NULL) {
        return -1;
    }

    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1) {
        perror("readlink");
        dbus_connection_unref(conn);
        return -1;
    }
    exe_path[len] = '\0';

    char unit_name[256];
    snprintf(unit_name, sizeof(unit_name), service_name);

    msg = dbus_message_new_method_call("org.freedesktop.systemd1",
                                       "/org/freedesktop/systemd1",
                                       "org.freedesktop.systemd1.Manager",
                                       "StartTransientUnit");
    if (msg == NULL) {
        fprintf(stderr, "Message Null\n");
        dbus_connection_unref(conn);
        return -1;
    }

    dbus_message_iter_init_append(msg, &args);

    const char *unit_name_ptr = unit_name;
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &unit_name_ptr)) {
        fprintf(stderr, "Out Of Memory!\n");
        dbus_message_unref(msg);
        dbus_connection_unref(conn);
        return -1;
    }

    const char *mode = "fail";
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &mode)) {
        fprintf(stderr, "Out Of Memory!\n");
        dbus_message_unref(msg);
        dbus_connection_unref(conn);
        return -1;
    }

    if (!dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "(sv)", &props)) {
        fprintf(stderr, "Out Of Memory!\n");
        dbus_message_unref(msg);
        dbus_connection_unref(conn);
        return -1;
    }

    if (!dbus_message_iter_open_container(&props, DBUS_TYPE_STRUCT, NULL, &prop)) {
        goto error;
    }
    const char *type_key = "Type";
    if (!dbus_message_iter_append_basic(&prop, DBUS_TYPE_STRING, &type_key)) {
        goto error;
    }
    if (!dbus_message_iter_open_container(&prop, DBUS_TYPE_VARIANT, "s", &variant)) {
        goto error;
    }
    const char *type_value = "exec";
    if (!dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &type_value)) {
        goto error;
    }
    if (!dbus_message_iter_close_container(&prop, &variant)) {
        goto error;
    }
    if (!dbus_message_iter_close_container(&props, &prop)) {
        goto error;
    }

    if (!dbus_message_iter_open_container(&props, DBUS_TYPE_STRUCT, NULL, &prop)) {
        goto error;
    }
    const char *exec_key = "ExecStart";
    if (!dbus_message_iter_append_basic(&prop, DBUS_TYPE_STRING, &exec_key)) {
        goto error;
    }
    if (!dbus_message_iter_open_container(&prop, DBUS_TYPE_VARIANT, "a(sasb)", &variant)) {
        goto error;
    }
    if (!dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "(sasb)", &exec_array)) {
        goto error;
    }
    if (!dbus_message_iter_open_container(&exec_array, DBUS_TYPE_STRUCT, NULL, &exec_struct)) {
        goto error;
    }
    const char *exe_path_ptr = exe_path;
    if (!dbus_message_iter_append_basic(&exec_struct, DBUS_TYPE_STRING, &exe_path_ptr)) {
        goto error;
    }
    if (!dbus_message_iter_open_container(&exec_struct, DBUS_TYPE_ARRAY, "s", &argv_array)) {
        goto error;
    }
    if (!dbus_message_iter_append_basic(&argv_array, DBUS_TYPE_STRING, &exe_path_ptr)) {
        goto error;
    }

    for (int i = 1; i < argc; i++) {
        if (!dbus_message_iter_append_basic(&argv_array, DBUS_TYPE_STRING, &argv[i])) {
            goto error;
        }
    }
    if (!dbus_message_iter_close_container(&exec_struct, &argv_array)) {
        goto error;
    }

    dbus_bool_t ignore_errors = FALSE;
    if (!dbus_message_iter_append_basic(&exec_struct, DBUS_TYPE_BOOLEAN, &ignore_errors)) {
        goto error;
    }
    if (!dbus_message_iter_close_container(&exec_array, &exec_struct)) {
        goto error;
    }
    if (!dbus_message_iter_close_container(&variant, &exec_array)) {
        goto error;
    }
    if (!dbus_message_iter_close_container(&prop, &variant)) {
        goto error;
    }
    if (!dbus_message_iter_close_container(&props, &prop)) {
        goto error;
    }

    if (!dbus_message_iter_open_container(&props, DBUS_TYPE_STRUCT, NULL, &prop)) {
        goto error;
    }
    const char *collect_key = "CollectMode";
    if (!dbus_message_iter_append_basic(&prop, DBUS_TYPE_STRING, &collect_key)) {
        goto error;
    }
    if (!dbus_message_iter_open_container(&prop, DBUS_TYPE_VARIANT, "s", &variant)) {
        goto error;
    }
    const char *collect_value = "inactive-or-failed";  // or just "inactive"
    if (!dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &collect_value)) {
        goto error;
    }
    if (!dbus_message_iter_close_container(&prop, &variant)) {
        goto error;
    }
    if (!dbus_message_iter_close_container(&props, &prop)) {
        goto error;
    }

    if (environment && environment[0]) {
        // Count environment variables
        int env_count = 0;
        while (environment[env_count]) {
            env_count++;
        }

        if (!dbus_message_iter_open_container(&props, DBUS_TYPE_STRUCT, NULL, &env_prop)) {
            goto error;
        }
        const char *env_key = "Environment";
        if (!dbus_message_iter_append_basic(&env_prop, DBUS_TYPE_STRING, &env_key)) {
            goto error;
        }
        if (!dbus_message_iter_open_container(&env_prop, DBUS_TYPE_VARIANT, "as", &env_variant)) {
            goto error;
        }
        if (!dbus_message_iter_open_container(&env_variant, DBUS_TYPE_ARRAY, "s", &env_array)) {
            goto error;
        }

        for (int i = 0; i < env_count; i++) {
            if (!dbus_message_iter_append_basic(&env_array, DBUS_TYPE_STRING, &environment[i])) {
                goto error;
            }
        }

        if (!dbus_message_iter_close_container(&env_variant, &env_array)) {
            goto error;
        }
        if (!dbus_message_iter_close_container(&env_prop, &env_variant)) {
            goto error;
        }
        if (!dbus_message_iter_close_container(&props, &env_prop)) {
            goto error;
        }
    }

    if (!dbus_message_iter_close_container(&args, &props)) {
        goto error;
    }

    if (!dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "(sa(sv))", &aux)) {
        goto error;
    }
    if (!dbus_message_iter_close_container(&args, &aux)) {
        goto error;
    }

    reply = dbus_connection_send_with_reply_and_block(conn, msg, -1, &err);

    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "Failed to start transient service: %s\n", err.message);
        dbus_error_free(&err);
        dbus_message_unref(msg);
        dbus_connection_unref(conn);
        return -1;
    }

    if (reply) {
        dbus_message_unref(reply);
    }

    printf("Started transient service: %s\n", unit_name);

    dbus_message_unref(msg);
    dbus_connection_unref(conn);
    return 0;

error:
    fprintf(stderr, "Failed to build DBus message\n");
    dbus_message_unref(msg);
    dbus_connection_unref(conn);
    return -1;
}
