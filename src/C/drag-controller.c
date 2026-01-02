#include "drag-controller.h"
#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <math.h>

struct _BobLauncherAppSettingsLayerShell {
    GTypeInstance parent_instance;
    volatile int ref_count;
    void * priv;
    GSettings* settings;
};

typedef struct _BobLauncherQueryContainer BobLauncherQueryContainer;
typedef struct _BobLauncherLauncherWindow BobLauncherLauncherWindow;
typedef struct _BobLauncherAppSettingsLayerShell BobLauncherAppSettingsLayerShell;

extern BobLauncherLauncherWindow *bob_launcher_app_main_win;
extern GdkRectangle *bob_launcher_utils_get_current_display_size(GtkWindow *window);

extern double bob_launcher_app_settings_layer_shell_get_anchor_point_x(BobLauncherAppSettingsLayerShell *self);
extern double bob_launcher_app_settings_layer_shell_get_anchor_point_y(BobLauncherAppSettingsLayerShell *self);
extern double bob_launcher_app_settings_layer_shell_get_anchor_snap_threshold(BobLauncherAppSettingsLayerShell *self);
extern GSettings *bob_launcher_app_settings_layer_shell_get_gsettings(BobLauncherAppSettingsLayerShell *self);
extern void bob_launcher_app_settings_layer_shell_change_points(BobLauncherAppSettingsLayerShell *self, double x, double y);

typedef gboolean (*BobLauncherShouldDrag)(double x, double y, gpointer user_data);

static GtkGestureDrag *drag_gesture = NULL;
static GtkGestureClick *click_controller = NULL;

static int window_container_diff;
static int monitor_start_x;
static int monitor_start_y;
static int monitor_width;
static int monitor_height;
static int prev_margin_x;
static int prev_margin_y;
static int drag_pt_within_container_x;
static int drag_pt_within_container_y;
static int container_width;
static int container_height;

static graphene_rect_t sm_rect;
static BobLauncherAppSettingsLayerShell *settings = NULL;
static BobLauncherShouldDrag should_drag = NULL;
static gpointer should_drag_data = NULL;
static GDestroyNotify should_drag_data_destroy = NULL;

static double last_x;
static double last_y;

static void
on_click_pressed(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data)
{
    BobLauncherAppSettingsLayerShell* ls = (BobLauncherAppSettingsLayerShell*)user_data;

    guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

    if (button == GDK_BUTTON_PRIMARY && n_press == 2) {
        GtkLayerShellKeyboardMode current = gtk_layer_get_keyboard_mode(GTK_WINDOW(bob_launcher_app_main_win));
        gboolean is_exclusive = (current == GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
        GtkLayerShellKeyboardMode new_mode = is_exclusive
            ? GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND
            : GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE;

        g_settings_set_enum(ls->settings, "keyboard-mode", (int)new_mode);

        gtk_gesture_set_state(GTK_GESTURE(drag_gesture), GTK_EVENT_SEQUENCE_DENIED);
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
        return;
    }

    if (n_press != 2) {
        return;
    }

    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    gtk_gesture_set_state(GTK_GESTURE(drag_gesture), GTK_EVENT_SEQUENCE_DENIED);

    GdkRectangle *rect = bob_launcher_utils_get_current_display_size(GTK_WINDOW(bob_launcher_app_main_win));
    if (rect == NULL) return;

    double anchor_x = bob_launcher_app_settings_layer_shell_get_anchor_point_x(ls);
    double anchor_y = bob_launcher_app_settings_layer_shell_get_anchor_point_y(ls);

    int target_x = (int)(rect->width * anchor_x - gtk_widget_get_width(GTK_WIDGET(bob_launcher_app_main_win)) / 2);
    int target_y = (int)(rect->height * anchor_y);

    gtk_layer_set_margin(GTK_WINDOW(bob_launcher_app_main_win), GTK_LAYER_SHELL_EDGE_LEFT, target_x);

    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    graphene_rect_t local_rect;
    gtk_widget_compute_bounds(widget, GTK_WIDGET(bob_launcher_app_main_win), &local_rect);
    int local_diff = (int)local_rect.origin.y;

    gtk_layer_set_margin(GTK_WINDOW(bob_launcher_app_main_win), GTK_LAYER_SHELL_EDGE_TOP, target_y - local_diff);
    bob_launcher_app_settings_layer_shell_change_points(ls, anchor_x, anchor_y);

    g_free(rect);
}

static void
on_drag_begin(GtkGestureDrag *gesture, double x, double y, gpointer user_data)
{
    (void)user_data;

    if (should_drag && !should_drag(x, y, should_drag_data)) {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
        return;
    }

    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    gtk_gesture_set_state(GTK_GESTURE(click_controller), GTK_EVENT_SEQUENCE_DENIED);

    graphene_point_t in_point = GRAPHENE_POINT_INIT((float)x, (float)y);
    graphene_point_t window_point;
    if (!gtk_widget_compute_point(GTK_WIDGET(bob_launcher_app_main_win),
                                   GTK_WIDGET(bob_launcher_app_main_win),
                                   &in_point, &window_point)) {
        return;
    }

    GdkRectangle *monitor_dimensions = bob_launcher_utils_get_current_display_size(GTK_WINDOW(bob_launcher_app_main_win));
    if (monitor_dimensions == NULL) {
        return;
    }

    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    gtk_widget_compute_bounds(widget, GTK_WIDGET(bob_launcher_app_main_win), &sm_rect);
    window_container_diff = (int)sm_rect.origin.y;

    GtkWidget *child = gtk_window_get_child(GTK_WINDOW(bob_launcher_app_main_win));
    graphene_rect_t main_rect;
    gtk_widget_compute_bounds(child, GTK_WIDGET(bob_launcher_app_main_win), &main_rect);
    container_height = (int)main_rect.size.height;
    container_width = (int)main_rect.size.width;

    monitor_start_y = monitor_dimensions->y;
    monitor_start_x = monitor_dimensions->x;
    monitor_width = monitor_dimensions->width;
    monitor_height = monitor_dimensions->height;

    prev_margin_x = gtk_layer_get_margin(GTK_WINDOW(bob_launcher_app_main_win), GTK_LAYER_SHELL_EDGE_LEFT);
    prev_margin_y = gtk_layer_get_margin(GTK_WINDOW(bob_launcher_app_main_win), GTK_LAYER_SHELL_EDGE_TOP);

    last_x = prev_margin_x;
    last_y = prev_margin_y + window_container_diff;

    drag_pt_within_container_x = (int)x;
    drag_pt_within_container_y = (int)y;

    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);

    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(bob_launcher_app_main_win));
    GdkCursor *cursor = gdk_cursor_new_from_name("grab", NULL);
    gdk_surface_set_cursor(surface, cursor);
    g_object_unref(cursor);

    g_free(monitor_dimensions);
}

static void
on_drag_update(GtkGestureDrag *gesture, double x, double y, gpointer user_data)
{
    (void)gesture;
    (void)user_data;

    double anchor_x = bob_launcher_app_settings_layer_shell_get_anchor_point_x(settings);
    double anchor_y = bob_launcher_app_settings_layer_shell_get_anchor_point_y(settings);
    int snap_threshold = bob_launcher_app_settings_layer_shell_get_anchor_snap_threshold(settings);

    int abs_x = (int)round(prev_margin_x + x);
    int h_center = (int)(monitor_width * anchor_x - sm_rect.size.width / 2 - sm_rect.origin.x);
    gboolean rightward = abs_x > last_x;

    int left_edge_snap = -(int)sm_rect.origin.x;
    int right_edge_snap = (int)(monitor_width - sm_rect.size.width - sm_rect.origin.x);

    if (abs_x >= h_center && abs_x <= h_center + snap_threshold && rightward) {
        abs_x = h_center;
    } else if (abs_x <= h_center && abs_x >= h_center - snap_threshold && !rightward) {
        abs_x = h_center;
    } else if (!rightward && left_edge_snap - snap_threshold < abs_x && abs_x < left_edge_snap) {
        abs_x = left_edge_snap;
    } else if (rightward && right_edge_snap < abs_x && abs_x < right_edge_snap + snap_threshold) {
        abs_x = right_edge_snap;
    }

    if (prev_margin_x != abs_x) {
        gtk_layer_set_margin(GTK_WINDOW(bob_launcher_app_main_win), GTK_LAYER_SHELL_EDGE_LEFT, abs_x);
        last_x = abs_x;
    }

    gboolean check_y_center_snap = (abs_x == h_center);
    int compare_pt = prev_margin_y + window_container_diff;
    int abs_y = (int)fmax(0.0, floor(compare_pt + y));

    gboolean downward = abs_y > last_y;

    int v_center = (int)(monitor_height * anchor_y);
    int bottom_edge_snap = (int)(monitor_height - sm_rect.size.height);

    if (check_y_center_snap && downward && abs_y >= v_center && abs_y <= v_center + snap_threshold) {
        abs_y = v_center;
    } else if (check_y_center_snap && !downward && abs_y <= v_center && abs_y >= v_center - snap_threshold) {
        abs_y = v_center;
    } else if (y > 0 && bottom_edge_snap < abs_y) {
        abs_y = bottom_edge_snap;
    } else {
        int soft_snap = (int)(monitor_height - container_height);
        if (downward && soft_snap < abs_y && abs_y < soft_snap + snap_threshold) {
            abs_y = soft_snap;
        }
    }

    if (prev_margin_y != abs_y - window_container_diff) {
        gtk_layer_set_margin(GTK_WINDOW(bob_launcher_app_main_win), GTK_LAYER_SHELL_EDGE_TOP, abs_y - window_container_diff);
        last_y = abs_y;
    }
}

static void
on_drag_end(GtkGestureDrag *gesture, double x, double y, gpointer user_data)
{
    (void)x;
    (void)y;
    (void)user_data;

    gtk_event_controller_reset(GTK_EVENT_CONTROLLER(gesture));

    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(bob_launcher_app_main_win));
    GdkCursor *cursor = gdk_cursor_new_from_name("default", NULL);
    gdk_surface_set_cursor(surface, cursor);
    g_object_unref(cursor);

    double min_x_percentage = -((container_width / 2.0) / monitor_width);
    double max_x_percentage = 1.0 - min_x_percentage;
    double min_y_percentage = -(window_container_diff / (double)monitor_height);
    double max_y_percentage = 1.0;

    int left_margin = gtk_layer_get_margin(GTK_WINDOW(bob_launcher_app_main_win), GTK_LAYER_SHELL_EDGE_LEFT);
    double center_x = left_margin + gtk_widget_get_width(GTK_WIDGET(bob_launcher_app_main_win)) / 2.0;
    double x_percentage = center_x / monitor_width;

    int top_margin = gtk_layer_get_margin(GTK_WINDOW(bob_launcher_app_main_win), GTK_LAYER_SHELL_EDGE_TOP);
    double y_percentage = ((double)top_margin + window_container_diff) / (double)monitor_height;

    if (!(min_x_percentage < x_percentage && x_percentage < max_x_percentage)) {
        x_percentage = x_percentage - floor(x_percentage);
    }
    if (!(min_y_percentage <= y_percentage && y_percentage <= max_y_percentage)) {
        y_percentage = y_percentage - floor(y_percentage);
    }

    bob_launcher_app_settings_layer_shell_change_points(settings, x_percentage, y_percentage);
}

void
bob_launcher_setup_click_controller(GtkWidget *widget, BobLauncherAppSettingsLayerShell *layer_settings)
{
    click_controller = GTK_GESTURE_CLICK(gtk_gesture_click_new());
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click_controller), 0);
    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(click_controller));

    g_signal_connect_data(click_controller, "pressed", (GCallback)on_click_pressed, layer_settings, NULL, 0);
}

void
bob_launcher_setup_drag_controller(BobLauncherQueryContainer *query_container,
                                    BobLauncherAppSettingsLayerShell *layer_settings,
                                    BobLauncherShouldDrag drag_func,
                                    gpointer drag_func_data,
                                    GDestroyNotify drag_func_data_destroy_notify)
{
    settings = layer_settings;
    should_drag = drag_func;
    should_drag_data = drag_func_data;
    should_drag_data_destroy = drag_func_data_destroy_notify;

    drag_gesture = GTK_GESTURE_DRAG(gtk_gesture_drag_new());
    gtk_widget_add_controller(GTK_WIDGET(query_container), GTK_EVENT_CONTROLLER(drag_gesture));

    g_signal_connect(drag_gesture, "drag-begin", G_CALLBACK(on_drag_begin), NULL);
    g_signal_connect(drag_gesture, "drag-update", G_CALLBACK(on_drag_update), NULL);
    g_signal_connect(drag_gesture, "drag-end", G_CALLBACK(on_drag_end), NULL);
}
