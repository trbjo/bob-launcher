#ifndef SYSTEMD_LAUNCHER_H
#define SYSTEMD_LAUNCHER_H

#include <stdbool.h>
#include <gio/gio.h>
#include <dbus/dbus.h>

typedef struct SystemdLauncher SystemdLauncher;

SystemdLauncher *systemd_launcher_new(DBusConnection *connection);
void systemd_launcher_free(SystemdLauncher *self);

bool systemd_launcher_launch_files(SystemdLauncher *self, GList *files, char **env);
bool systemd_launcher_launch_uris(SystemdLauncher *self, GList *uris, char **env);
bool systemd_launcher_launch_with_files(SystemdLauncher *self, GAppInfo *app_info, GList *files, const char *action, char **env);
bool systemd_launcher_launch_with_uris(SystemdLauncher *self, GAppInfo *app_info, GList *uris, const char *action, char **env);
bool systemd_launcher_launch_command(SystemdLauncher *self, const char *identifier, char **argv, char **env, bool blocking, bool needs_terminal);

#endif /* SYSTEMD_LAUNCHER_H */
