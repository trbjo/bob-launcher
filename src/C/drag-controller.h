#ifndef BOB_LAUNCHER_DRAG_CONTROLLER_H
#define BOB_LAUNCHER_DRAG_CONTROLLER_H

#include <gtk/gtk.h>

typedef struct _BobLauncherQueryContainer BobLauncherQueryContainer;
typedef struct _BobLauncherAppSettingsLayerShell BobLauncherAppSettingsLayerShell;

typedef gboolean (*BobLauncherShouldDrag)(double x, double y, gpointer user_data);

void bob_launcher_setup_click_controller(GtkWidget *widget,
                                          BobLauncherAppSettingsLayerShell *layer_settings);

void bob_launcher_setup_drag_controller(BobLauncherQueryContainer *query_container,
                                         BobLauncherAppSettingsLayerShell *layer_settings,
                                         BobLauncherShouldDrag drag_func,
                                         gpointer drag_func_data,
                                         GDestroyNotify drag_func_data_destroy);

#endif
