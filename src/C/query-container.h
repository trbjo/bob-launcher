#pragma once

G_BEGIN_DECLS

#define BOB_LAUNCHER_QUERY_CONTAINER(obj) ((BobLauncherQueryContainer*)obj)

typedef struct _BobLauncherQueryContainer BobLauncherQueryContainer;
typedef struct _BobLauncherQueryContainerClass BobLauncherQueryContainerClass;

GType bob_launcher_query_container_get_type(void) G_GNUC_CONST;
BobLauncherQueryContainer *bob_launcher_query_container_new(void);
void bob_launcher_query_container_adjust_label_for_query();

G_END_DECLS
