#ifndef DBUS_SCOPE_MONITOR_H
#define DBUS_SCOPE_MONITOR_H

#include <sys/types.h>

typedef enum {
    DBUS_SCOPE_EVENT_CONNECTED,
    DBUS_SCOPE_EVENT_DISCONNECTED
} DBusScopeEventType;

typedef void (*DBusScopeCallback)(DBusScopeEventType event_type,
                                  const char* dbus_name,
                                  const char* object_path,
                                  void* data);

typedef struct DBusScopeMonitor DBusScopeMonitor;

DBusScopeMonitor* dbus_scope_monitor_new(DBusScopeCallback callback, void* user_data);
int dbus_scope_monitor_start(DBusScopeMonitor* monitor);
void dbus_scope_monitor_stop(DBusScopeMonitor* monitor);
void dbus_scope_monitor_free(DBusScopeMonitor* monitor);

#endif /* DBUS_SCOPE_MONITOR_H */
