#include <dbus/dbus.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdatomic.h>

typedef enum {
    DBUS_SCOPE_EVENT_CONNECTED,
    DBUS_SCOPE_EVENT_DISCONNECTED
} DBusScopeEventType;

typedef void (*DBusScopeCallback)(DBusScopeEventType event_type,
                                  const char* dbus_name,
                                  const char* object_path,
                                  void* data);

typedef struct {
    char* dbus_name;
    char* unique_name;
    int waiting_for_app_interface;
} PendingConnection;

typedef struct {
    DBusConnection* conn;
    DBusScopeCallback callback;
    void* user_data;
    pthread_t thread;
    atomic_int should_stop;
    PendingConnection* pending_connections;
    size_t pending_count;
    size_t pending_capacity;
    pthread_mutex_t pending_mutex;
} DBusScopeMonitor;

static bool is_valid_dbus_name(const char* name) {
    return (strchr(name, '.') != NULL) && (name[0] != ':');
}

static char* find_app_recursive(DBusConnection* conn, const char* dbus_name, const char* path) {
    DBusMessage *msg, *reply;
    DBusError err;
    dbus_error_init(&err);

    msg = dbus_message_new_method_call(dbus_name, path, "org.freedesktop.DBus.Introspectable", "Introspect");
    reply = dbus_connection_send_with_reply_and_block(conn, msg, 1000, &err);
    dbus_message_unref(msg);
    if (dbus_error_is_set(&err) || !reply) {
        dbus_error_free(&err);
        return NULL;
    }

    const char* xml;
    char* app_object_path = NULL;
    if (dbus_message_get_args(reply, &err, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID)) {
        if (strstr(xml, "org.freedesktop.Application")) {
            app_object_path = strdup(path);
        } else {
            const char* node_start = xml;
            while ((node_start = strstr(node_start, "<node name=\"")) != NULL && !app_object_path) {
                node_start += 12;
                const char* node_end = strchr(node_start, '"');
                if (!node_end) break;

                int len = node_end - node_start;
                char* child_path = malloc(strlen(path) + len + 2);
                sprintf(child_path, "%s%s%s", strcmp(path, "/") ? path : "", "/", "");
                strncat(child_path, node_start, len);

                app_object_path = find_app_recursive(conn, dbus_name, child_path);
                free(child_path);
                node_start = node_end;
            }
        }
    }
    dbus_message_unref(reply);
    dbus_error_free(&err);
    return app_object_path;
}

static void add_pending_connection(DBusScopeMonitor* monitor, const char* dbus_name, const char* unique_name) {
    pthread_mutex_lock(&monitor->pending_mutex);

    if (monitor->pending_count >= monitor->pending_capacity) {
        monitor->pending_capacity = monitor->pending_capacity ? monitor->pending_capacity * 2 : 4;
        monitor->pending_connections = realloc(monitor->pending_connections,
                                               sizeof(PendingConnection) * monitor->pending_capacity);
    }

    PendingConnection* pending = &monitor->pending_connections[monitor->pending_count++];
    pending->dbus_name = strdup(dbus_name);
    pending->unique_name = strdup(unique_name);
    pending->waiting_for_app_interface = 1;

    pthread_mutex_unlock(&monitor->pending_mutex);
}

static void remove_pending_connection(DBusScopeMonitor* monitor, size_t index) {
    pthread_mutex_lock(&monitor->pending_mutex);

    PendingConnection* pending = &monitor->pending_connections[index];
    free(pending->dbus_name);
    free(pending->unique_name);

    memmove(&monitor->pending_connections[index],
            &monitor->pending_connections[index + 1],
            sizeof(PendingConnection) * (monitor->pending_count - index - 1));

    monitor->pending_count--;

    pthread_mutex_unlock(&monitor->pending_mutex);
}

static void setup_interface_monitoring(DBusScopeMonitor* monitor, const char* unique_name) {
    DBusError err;
    dbus_error_init(&err);

    char match[512];
    snprintf(match, sizeof(match),
             "type='signal',sender='%s',interface='org.freedesktop.DBus.ObjectManager',"
             "member='InterfacesAdded'",
             unique_name);

    dbus_bus_add_match(monitor->conn, match, &err);

    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "Failed to add InterfacesAdded match: %s\n", err.message);
        dbus_error_free(&err);
    }
}

static char* find_application_object_path(DBusConnection* conn, const char* dbus_name) {
    return find_app_recursive(conn, dbus_name, "/");
}

static DBusHandlerResult message_filter(DBusConnection* conn, DBusMessage* msg, void* user_data) {
    DBusScopeMonitor* monitor = (DBusScopeMonitor*)user_data;

    if (dbus_message_is_signal(msg, "com.example.DBusScopeMonitor", "WakeUp")) {
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (dbus_message_is_signal(msg, "org.freedesktop.DBus.ObjectManager", "InterfacesAdded")) {
        DBusMessageIter iter, dict_iter;
        const char* object_path;
        const char* sender = dbus_message_get_sender(msg);

        if (dbus_message_iter_init(msg, &iter) &&
            dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_OBJECT_PATH) {

            dbus_message_iter_get_basic(&iter, &object_path);
            dbus_message_iter_next(&iter);

            if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY) {
                dbus_message_iter_recurse(&iter, &dict_iter);

                while (dbus_message_iter_get_arg_type(&dict_iter) == DBUS_TYPE_DICT_ENTRY) {
                    DBusMessageIter entry_iter;
                    const char* interface_name;

                    dbus_message_iter_recurse(&dict_iter, &entry_iter);
                    dbus_message_iter_get_basic(&entry_iter, &interface_name);

                    if (strcmp(interface_name, "org.freedesktop.Application") == 0) {
                        pthread_mutex_lock(&monitor->pending_mutex);
                        for (size_t i = 0; i < monitor->pending_count; i++) {
                            PendingConnection* pending = &monitor->pending_connections[i];
                            if (strcmp(pending->unique_name, sender) == 0) {
                                monitor->callback(DBUS_SCOPE_EVENT_CONNECTED,
                                                  pending->dbus_name,
                                                  object_path,
                                                  monitor->user_data);

                                pthread_mutex_unlock(&monitor->pending_mutex);
                                remove_pending_connection(monitor, i);
                                return DBUS_HANDLER_RESULT_HANDLED;
                            }
                        }
                        pthread_mutex_unlock(&monitor->pending_mutex);
                    }

                    dbus_message_iter_next(&dict_iter);
                }
            }
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (dbus_message_is_signal(msg, "org.freedesktop.DBus", "NameOwnerChanged")) {
        DBusMessageIter args;
        char *name, *old_owner, *new_owner;

        if (dbus_message_iter_init(msg, &args) &&
            dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_STRING) {

            dbus_message_iter_get_basic(&args, &name);
            dbus_message_iter_next(&args);
            dbus_message_iter_get_basic(&args, &old_owner);
            dbus_message_iter_next(&args);
            dbus_message_iter_get_basic(&args, &new_owner);

            if (strlen(old_owner) > 0) {
                monitor->callback(DBUS_SCOPE_EVENT_DISCONNECTED,
                                  name, NULL, monitor->user_data);
            }

            if (is_valid_dbus_name(name) && strlen(new_owner) > 0) {
                char* object_path = find_application_object_path(monitor->conn, new_owner);
                if (object_path) {
                    monitor->callback(DBUS_SCOPE_EVENT_CONNECTED,
                                      name, object_path, monitor->user_data);
                    free(object_path);
                } else {
                    add_pending_connection(monitor, name, new_owner);
                    setup_interface_monitoring(monitor, new_owner);
                }
            }
        }
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void scan_existing_names(DBusScopeMonitor* monitor) {
    DBusMessage* msg;
    DBusMessage* reply;
    DBusError err;
    DBusMessageIter iter, array_iter;

    dbus_error_init(&err);

    msg = dbus_message_new_method_call("org.freedesktop.DBus",
                                       "/org/freedesktop/DBus",
                                       "org.freedesktop.DBus",
                                       "ListNames");

    reply = dbus_connection_send_with_reply_and_block(monitor->conn, msg, -1, &err);
    dbus_message_unref(msg);

    if (dbus_error_is_set(&err) || !reply) {
        fprintf(stderr, "Failed to list names: %s\n", err.message);
        dbus_error_free(&err);
        return;
    }

    if (dbus_message_iter_init(reply, &iter) &&
        dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY) {

        dbus_message_iter_recurse(&iter, &array_iter);

        while (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_STRING) {
            const char* name;
            dbus_message_iter_get_basic(&array_iter, &name);

            if (is_valid_dbus_name(name) && strcmp(name, "org.freedesktop.DBus") != 0) {
                DBusMessage* owner_msg = dbus_message_new_method_call(
                    "org.freedesktop.DBus",
                    "/org/freedesktop/DBus",
                    "org.freedesktop.DBus",
                    "GetNameOwner");

                dbus_message_append_args(owner_msg,
                                         DBUS_TYPE_STRING, &name,
                                         DBUS_TYPE_INVALID);

                DBusMessage* owner_reply = dbus_connection_send_with_reply_and_block(
                    monitor->conn, owner_msg, -1, &err);

                if (!dbus_error_is_set(&err) && owner_reply) {
                    const char* owner;
                    if (dbus_message_get_args(owner_reply, &err,
                                              DBUS_TYPE_STRING, &owner,
                                              DBUS_TYPE_INVALID)) {
                        char* object_path = find_application_object_path(monitor->conn, owner);

                        if (object_path) {
                            monitor->callback(DBUS_SCOPE_EVENT_CONNECTED,
                                              name, object_path, monitor->user_data);
                            free(object_path);
                        } else {
                            add_pending_connection(monitor, name, owner);
                            setup_interface_monitoring(monitor, owner);
                        }
                    }
                    dbus_message_unref(owner_reply);
                }

                dbus_message_unref(owner_msg);
                dbus_error_free(&err);
                dbus_error_init(&err);
            }

            dbus_message_iter_next(&array_iter);
        }
    }

    dbus_message_unref(reply);
    dbus_error_free(&err);
}

static void* monitor_thread(void* arg) {
    DBusScopeMonitor* monitor = (DBusScopeMonitor*)arg;
    DBusError err;

    dbus_error_init(&err);

    dbus_bus_add_match(monitor->conn,
                       "type='signal',interface='org.freedesktop.DBus',"
                       "member='NameOwnerChanged'",
                       &err);

    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "Failed to add match: %s\n", err.message);
        dbus_error_free(&err);
        return NULL;
    }

    dbus_bus_add_match(monitor->conn,
                       "type='signal',interface='com.example.DBusScopeMonitor',"
                       "member='WakeUp'",
                       &err);

    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "Failed to add match: %s\n", err.message);
        dbus_error_free(&err);
        return NULL;
    }

    dbus_bus_add_match(monitor->conn,
                       "type='signal',interface='org.freedesktop.DBus.ObjectManager',"
                       "member='InterfacesAdded'",
                       &err);

    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "Failed to add InterfacesAdded match: %s\n", err.message);
        dbus_error_free(&err);
        return NULL;
    }

    dbus_connection_add_filter(monitor->conn, message_filter, monitor, NULL);
    dbus_connection_flush(monitor->conn);

    scan_existing_names(monitor);

    while (!atomic_load(&monitor->should_stop) &&
           dbus_connection_read_write_dispatch(monitor->conn, -1)) {
        ;
    }

    dbus_connection_remove_filter(monitor->conn, message_filter, monitor);

    return NULL;
}

DBusScopeMonitor* dbus_scope_monitor_new(DBusScopeCallback callback, void* user_data) {
    DBusScopeMonitor* monitor;
    DBusError err;

    if (!callback) {
        fprintf(stderr, "Callback is required\n");
        return NULL;
    }

    monitor = calloc(1, sizeof(DBusScopeMonitor));
    if (!monitor) {
        return NULL;
    }

    dbus_error_init(&err);

    monitor->conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "Failed to connect to D-Bus: %s\n", err.message);
        dbus_error_free(&err);
        free(monitor);
        return NULL;
    }

    monitor->callback = callback;
    monitor->user_data = user_data;
    atomic_store(&monitor->should_stop, 0);
    pthread_mutex_init(&monitor->pending_mutex, NULL);
    monitor->pending_connections = NULL;
    monitor->pending_count = 0;
    monitor->pending_capacity = 0;

    return monitor;
}

int dbus_scope_monitor_start(DBusScopeMonitor* monitor) {
    if (!monitor || monitor->thread) {
        return -1;
    }

    if (pthread_create(&monitor->thread, NULL, monitor_thread, monitor) != 0) {
        fprintf(stderr, "Failed to create monitor thread\n");
        return -1;
    }

    return 0;
}

void dbus_scope_monitor_stop(DBusScopeMonitor* monitor) {
    if (!monitor) {
        return;
    }

    atomic_store(&monitor->should_stop, 1);

    if (monitor->thread) {
        DBusMessage* signal = dbus_message_new_signal(
            "/com/example/DBusScopeMonitor",
            "com.example.DBusScopeMonitor",
            "WakeUp");

        dbus_connection_send(monitor->conn, signal, NULL);
        dbus_message_unref(signal);
        dbus_connection_flush(monitor->conn);

        pthread_join(monitor->thread, NULL);
        monitor->thread = 0;
    }
}

void dbus_scope_monitor_free(DBusScopeMonitor* monitor) {
    if (!monitor) {
        return;
    }

    dbus_scope_monitor_stop(monitor);

    if (monitor->conn) {
        dbus_connection_close(monitor->conn);
        dbus_connection_unref(monitor->conn);
    }

    pthread_mutex_lock(&monitor->pending_mutex);
    for (size_t i = 0; i < monitor->pending_count; i++) {
        free(monitor->pending_connections[i].dbus_name);
        free(monitor->pending_connections[i].unique_name);
    }
    free(monitor->pending_connections);
    pthread_mutex_unlock(&monitor->pending_mutex);

    pthread_mutex_destroy(&monitor->pending_mutex);

    free(monitor);
}
