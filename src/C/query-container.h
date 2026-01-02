#ifndef BOB_LAUNCHER_QUERY_CONTAINER_H
#define BOB_LAUNCHER_QUERY_CONTAINER_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define BOB_LAUNCHER_TYPE_QUERY_CONTAINER (bob_launcher_query_container_get_type())
#define BOB_LAUNCHER_QUERY_CONTAINER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), BOB_LAUNCHER_TYPE_QUERY_CONTAINER, BobLauncherQueryContainer))
#define BOB_LAUNCHER_IS_QUERY_CONTAINER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), BOB_LAUNCHER_TYPE_QUERY_CONTAINER))

typedef struct _BobLauncherQueryContainer BobLauncherQueryContainer;
typedef struct _BobLauncherQueryContainerClass BobLauncherQueryContainerClass;

GType bob_launcher_query_container_get_type(void) G_GNUC_CONST;
BobLauncherQueryContainer *bob_launcher_query_container_new(void);
void bob_launcher_query_container_set_cursor_position(BobLauncherQueryContainer *self, int position);
void bob_launcher_query_container_adjust_label_for_query(const char *text, int cursor_position);

G_END_DECLS

#endif
