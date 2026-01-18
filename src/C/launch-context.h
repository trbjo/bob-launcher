#ifndef BOB_LAUNCHER_LAUNCH_CONTEXT_H
#define BOB_LAUNCHER_LAUNCH_CONTEXT_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define BOB_LAUNCHER_TYPE_LAUNCH_CONTEXT (bob_launcher_bob_launch_context_get_type())
#define BOB_LAUNCHER_LAUNCH_CONTEXT(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), BOB_LAUNCHER_TYPE_LAUNCH_CONTEXT, BobLauncherLaunchContext))
#define BOB_LAUNCHER_IS_LAUNCH_CONTEXT(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), BOB_LAUNCHER_TYPE_LAUNCH_CONTEXT))

typedef struct _BobLauncherLaunchContext BobLauncherLaunchContext;
typedef struct _BobLauncherLaunchContextClass BobLauncherLaunchContextClass;

GType bob_launcher_bob_launch_context_get_type(void) G_GNUC_CONST;

BobLauncherLaunchContext *bob_launcher_bob_launch_context_get_instance(void);

gboolean bob_launcher_bob_launch_context_launch_command(BobLauncherLaunchContext *self,
                                                     const gchar *identifier,
                                                     gchar **argv,
                                                     gboolean blocking,
                                                     gboolean needs_terminal);

gboolean bob_launcher_bob_launch_context_launch_file(BobLauncherLaunchContext *self,
                                                  GFile *file);

gboolean bob_launcher_bob_launch_context_launch_files_internal(BobLauncherLaunchContext *self,
                                                            GList *files,
                                                            gchar **env);

gboolean bob_launcher_bob_launch_context_launch_uris_internal(BobLauncherLaunchContext *self,
                                                           GList *uris,
                                                           gchar **env);

gboolean bob_launcher_bob_launch_context_launch_uri(BobLauncherLaunchContext *self,
                                                 const gchar *uri);

gboolean bob_launcher_bob_launch_context_launch_app(BobLauncherLaunchContext *self,
                                                 GAppInfo *app_info,
                                                 gboolean needs_terminal,
                                                 const gchar *action);

gboolean bob_launcher_bob_launch_context_launch_with_uri(BobLauncherLaunchContext *self,
                                                      GAppInfo *app_info,
                                                      const gchar *uri,
                                                      const gchar *action);

gboolean bob_launcher_bob_launch_context_launch_app_with_files(BobLauncherLaunchContext *self,
                                                            GAppInfo *app_info,
                                                            GList *files,
                                                            const gchar *action);

G_END_DECLS

#endif /* BOB_LAUNCHER_LAUNCH_CONTEXT_H */
