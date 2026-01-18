#ifndef COMMON_LAUNCHER_H
#define COMMON_LAUNCHER_H

#include <stdbool.h>
#include <gio/gio.h>

typedef struct CommonLauncher CommonLauncher;

CommonLauncher *common_launcher_new(void);
void common_launcher_free(CommonLauncher *self);

bool common_launcher_launch_files(CommonLauncher *self, GList *files, char **env);
bool common_launcher_launch_uris(CommonLauncher *self, GList *uris, char **env);
bool common_launcher_launch_with_files(CommonLauncher *self, GAppInfo *app_info, GList *files, const char *action, char **env);
bool common_launcher_launch_with_uris(CommonLauncher *self, GAppInfo *app_info, GList *uris, const char *action, char **env);
bool common_launcher_launch_command(CommonLauncher *self, const char *identifier, char **argv, char **env, bool blocking, bool needs_terminal);

#endif /* COMMON_LAUNCHER_H */
