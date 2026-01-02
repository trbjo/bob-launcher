#ifndef BOB_LAUNCHER_MAIN_CONTAINER_H
#define BOB_LAUNCHER_MAIN_CONTAINER_H

#include <gtk/gtk.h>
#include "hashset.h"

G_BEGIN_DECLS

#define BOB_LAUNCHER_TYPE_MAIN_CONTAINER (bob_launcher_main_container_get_type())
#define BOB_LAUNCHER_MAIN_CONTAINER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), BOB_LAUNCHER_TYPE_MAIN_CONTAINER, BobLauncherMainContainer))
#define BOB_LAUNCHER_IS_MAIN_CONTAINER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), BOB_LAUNCHER_TYPE_MAIN_CONTAINER))

typedef struct _BobLauncherMainContainer BobLauncherMainContainer;
typedef struct _BobLauncherMainContainerClass BobLauncherMainContainerClass;

GType bob_launcher_main_container_get_type(void) G_GNUC_CONST;
BobLauncherMainContainer *bob_launcher_main_container_new(void);
void bob_launcher_main_container_update_layout(HashSet *provider, gint selected_index);

G_END_DECLS

#endif /* BOB_LAUNCHER_MAIN_CONTAINER_H */
