#include "launcher-window.h"
#include "bob-launcher.h"
#include "constants.h"
#include <main-container.h>
#include <state.h>
#include <controller.h>
#include <gtk4-layer-shell.h>
#include <gdk/wayland/gdkwayland.h>
#include <glib-unix.h>
#include <wayland-client.h>
#include <simple-keyboard.h>
#include <simple-blur.h>
#include <cairo.h>
#include <math.h>
#include "gsk-utils.h"

/* ============================================================================
 * External type declarations
 * ============================================================================ */

typedef struct _BobLauncherAppSettings BobLauncherAppSettings;
typedef struct _BobLauncherAppSettingsLayerShell BobLauncherAppSettingsLayerShell;

typedef struct _BobLauncherAppSettingsLayerShellPrivate BobLauncherAppSettingsLayerShellPrivate;

struct _BobLauncherAppSettingsLayerShell {
    GTypeInstance parent_instance;
    volatile int ref_count;
    BobLauncherAppSettingsLayerShellPrivate *priv;
    GSettings *settings;
};

/* ============================================================================
 * External function declarations
 * ============================================================================ */

extern BobLauncherAppSettings *bob_launcher_app_settings_get_default(void);
extern BobLauncherAppSettingsLayerShell *bob_launcher_app_settings_get_layershell(BobLauncherAppSettings *self);
extern gboolean bob_launcher_app_settings_layer_shell_get_enabled(BobLauncherAppSettingsLayerShell *self);
extern gdouble bob_launcher_app_settings_layer_shell_get_point_x(BobLauncherAppSettingsLayerShell *self);
extern gdouble bob_launcher_app_settings_layer_shell_get_point_y(BobLauncherAppSettingsLayerShell *self);
extern gpointer bob_launcher_app_settings_ref(gpointer instance);
extern void bob_launcher_app_settings_unref(gpointer instance);

/* Resize handle types and constructors */
extern GType bob_launcher_up_down_resize_handle_get_type(void);
extern GType bob_launcher_width_resize_handle_get_type(void);
extern BobLauncherUpDownResizeHandle *bob_launcher_up_down_resize_handle_new(void);
extern BobLauncherWidthResizeHandle *bob_launcher_width_resize_handle_new(void);

/* MainContainer - declared in main-container.h */

/* Utils */
extern GdkRectangle *bob_launcher_utils_get_current_display_size(GtkWindow *window);

/* ============================================================================
 * External keyboard handle
 * ============================================================================ */

extern SimpleKeyboard *bob_launcher_app_keyboard;

/* ============================================================================
 * LayerShell helpers
 * ============================================================================ */

static bool layershell_wants_layer_shell(BobLauncherAppSettingsLayerShell *ls) {
    if (!bob_launcher_app_settings_layer_shell_get_enabled(ls))
        return FALSE;

    if (!gtk_layer_is_supported()) {
        g_warning("GtkLayerShell is not supported on your system");
        return FALSE;
    }
    return TRUE;
}

static void layershell_on_keyboard_mode_changed(GSettings *settings, const gchar *key, gpointer user_data) {
    GtkWindow *win = GTK_WINDOW(user_data);
    gint kb_mode = g_settings_get_enum(settings, "keyboard-mode");
    gtk_layer_set_keyboard_mode(win, (GtkLayerShellKeyboardMode)kb_mode);
}

static void layershell_setup(GtkWindow *win, BobLauncherAppSettings *appsettings) {
    BobLauncherAppSettingsLayerShell *ls = bob_launcher_app_settings_get_layershell(appsettings);

    if (!layershell_wants_layer_shell(ls))
        return;

    if (!gtk_layer_is_supported())
        g_error("Layershell is not supported on your system, disable it in settings");

    if (gtk_widget_get_realized(GTK_WIDGET(win)))
        g_error("layershell_setup must be called before the window is realized");

    gtk_layer_init_for_window(win);
    gtk_layer_set_namespace(win, BOB_LAUNCHER_APP_ID);
    gtk_layer_set_layer(win, GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_anchor(win, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(win, GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
    gtk_layer_set_anchor(win, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_exclusive_zone(win, -1);

    gint kb_mode = g_settings_get_enum(ls->settings, "keyboard-mode");
    gtk_layer_set_keyboard_mode(win, (GtkLayerShellKeyboardMode)kb_mode);

    g_signal_connect(ls->settings, "changed::keyboard-mode",
                     G_CALLBACK(layershell_on_keyboard_mode_changed), win);
}

static void layershell_adjust_margins(GtkWindow *win, int width, int height) {
    if (!gtk_layer_is_layer_window(win))
        return;

    GtkWidget *child = gtk_window_get_child(win);
    graphene_point_t container_point;
    if (!gtk_widget_compute_point(child, GTK_WIDGET(win), &GRAPHENE_POINT_INIT(0, 0), &container_point))
        return;

    int offset = (int)container_point.y;
    BobLauncherAppSettings *settings = bob_launcher_app_settings_get_default();

    GdkRectangle *rect = bob_launcher_utils_get_current_display_size(win);
    if (rect == NULL) return;

    double base_width = (double)width;
    double rect_width = (double)rect->width;

    BobLauncherAppSettingsLayerShell *ls = bob_launcher_app_settings_get_layershell(settings);
    double center_x = bob_launcher_app_settings_layer_shell_get_point_x(ls) * rect_width;
    int center_y = (int)(bob_launcher_app_settings_layer_shell_get_point_y(ls) * (double)rect->height);

    int left_margin = (int)round(center_x - (base_width / 2.0));
    gtk_layer_set_margin(win, GTK_LAYER_SHELL_EDGE_LEFT, left_margin);
    gtk_layer_set_margin(win, GTK_LAYER_SHELL_EDGE_TOP, center_y - offset);

    g_boxed_free(GDK_TYPE_RECTANGLE, rect);
}

/* ============================================================================
 * LauncherWindow Private Data
 * ============================================================================ */

struct _BobLauncherLauncherWindowPrivate {
    graphene_rect_t inner;
    GdkWaylandToplevel *surf;
    GSettings *settings;
    blur_surface *blur;
    GskSurfaceData *surface_data;

    gboolean client_side_shadow;
    gboolean inhibit_system_shortcuts;
    GdkMonitor *current_monitor;
};

/* ============================================================================
 * Static/Class variables
 * ============================================================================ */

static gpointer bob_launcher_launcher_window_parent_class = NULL;
static gint BobLauncherLauncherWindow_private_offset;

static BobLauncherAppSettings *launcher_window_appsettings = NULL;

/* Public static handles */
BobLauncherUpDownResizeHandle *bob_launcher_launcher_window_up_down_handle = NULL;
BobLauncherWidthResizeHandle *bob_launcher_launcher_window_width_handle = NULL;

/* ============================================================================
 * Property IDs
 * ============================================================================ */

enum {
    PROP_0,
    PROP_CLIENT_SIDE_SHADOW,
    PROP_INHIBIT_SYSTEM_SHORTCUTS,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL };

/* ============================================================================
 * Private helpers
 * ============================================================================ */

static inline BobLauncherLauncherWindowPrivate *
bob_launcher_launcher_window_get_instance_private(BobLauncherLauncherWindow *self)
{
    return G_STRUCT_MEMBER_P(self, BobLauncherLauncherWindow_private_offset);
}

static void
handle_shadow_settings(BobLauncherLauncherWindow *self)
{
    if (self->priv->client_side_shadow)
        gtk_widget_add_css_class(GTK_WIDGET(self), "client-side-shadow");
    else
        gtk_widget_remove_css_class(GTK_WIDGET(self), "client-side-shadow");
}

static void
handle_border_settings(BobLauncherLauncherWindow *self)
{
    if (g_settings_get_boolean(self->priv->settings, "client-side-border"))
        gtk_widget_add_css_class(GTK_WIDGET(self), "client-side-border");
    else
        gtk_widget_remove_css_class(GTK_WIDGET(self), "client-side-border");
}

static void
handle_shortcut_inhibit(BobLauncherLauncherWindow *self)
{
    if (self->priv->surf == NULL)
        return;

    if (self->priv->inhibit_system_shortcuts && gtk_widget_get_visible(GTK_WIDGET(self)))
        gdk_toplevel_inhibit_system_shortcuts(GDK_TOPLEVEL(self->priv->surf), NULL);
    else
        gdk_toplevel_restore_system_shortcuts(GDK_TOPLEVEL(self->priv->surf));
}

static void
on_shadow_settings_changed(GSettings *settings, const gchar *key, gpointer user_data)
{
    handle_shadow_settings(BOB_LAUNCHER_LAUNCHER_WINDOW(user_data));
}

static void
on_border_settings_changed(GSettings *settings, const gchar *key, gpointer user_data)
{
    handle_border_settings(BOB_LAUNCHER_LAUNCHER_WINDOW(user_data));
}

static void
on_inhibit_settings_changed(GSettings *settings, const gchar *key, gpointer user_data)
{
    handle_shortcut_inhibit(BOB_LAUNCHER_LAUNCHER_WINDOW(user_data));
}

static void
disable_controllers(GtkWidget *widget)
{
    GListModel *controllers = gtk_widget_observe_controllers(widget);
    guint n = g_list_model_get_n_items(controllers);

    for (gint i = (gint)n - 1; i >= 0; i--) {
        GtkEventController *controller = g_list_model_get_item(controllers, (guint)i);
        if (controller != NULL) {
            gtk_widget_remove_controller(widget, controller);
            g_object_unref(controller);
        }
    }
    g_object_unref(controllers);
}

static gboolean
on_monitor_changed_idle(gpointer user_data)
{
    BobLauncherLauncherWindow *self = BOB_LAUNCHER_LAUNCHER_WINDOW(user_data);
    layershell_adjust_margins(GTK_WINDOW(self),
        gtk_widget_get_width(GTK_WIDGET(self)),
        gtk_widget_get_height(GTK_WIDGET(self)));
    return G_SOURCE_REMOVE;
}

static void
on_monitor_changed(GdkSurface *surface, GdkMonitor *monitor, gpointer user_data)
{
    BobLauncherLauncherWindow *self = BOB_LAUNCHER_LAUNCHER_WINDOW(user_data);

    if (monitor == self->priv->current_monitor)
        return;

    self->priv->current_monitor = monitor;

    g_idle_add_full(G_PRIORITY_LOW, on_monitor_changed_idle, user_data, NULL);
}

/* ============================================================================
 * GtkWidget virtual functions
 * ============================================================================ */

static void
bob_launcher_launcher_window_size_allocate(GtkWidget *widget, gint width, gint height, gint baseline)
{
    BobLauncherLauncherWindow *self = (BobLauncherLauncherWindow*)widget;
    BobLauncherLauncherWindowPrivate *priv = self->priv;

    GTK_WIDGET_CLASS(bob_launcher_launcher_window_parent_class)->size_allocate(widget, width, height, baseline);

    GtkWidget *child = gtk_window_get_child(GTK_WINDOW(self));
    if (!gtk_widget_compute_bounds(child, widget, &priv->inner))
        g_critical("could not calculate bounds");

    gint width_handle_width, up_down_handle_height;
    gtk_widget_measure(GTK_WIDGET(bob_launcher_launcher_window_width_handle),
                       GTK_ORIENTATION_HORIZONTAL, -1, NULL, &width_handle_width, NULL, NULL);
    gtk_widget_measure(GTK_WIDGET(bob_launcher_launcher_window_up_down_handle),
                       GTK_ORIENTATION_VERTICAL, -1, NULL, &up_down_handle_height, NULL, NULL);

    graphene_point_t pt;
    pt.x = priv->inner.origin.x;
    pt.y = priv->inner.size.height + priv->inner.origin.y - up_down_handle_height;
    gtk_widget_allocate(GTK_WIDGET(bob_launcher_launcher_window_up_down_handle),
                        (gint)priv->inner.size.width, up_down_handle_height, baseline,
                        gsk_transform_translate(NULL, &pt));

    pt.x = priv->inner.size.width + priv->inner.origin.x - width_handle_width;
    pt.y = priv->inner.origin.y;
    gtk_widget_allocate(GTK_WIDGET(bob_launcher_launcher_window_width_handle),
                        width_handle_width, (gint)priv->inner.size.height, baseline,
                        gsk_transform_translate(NULL, &pt));
}

static void
bob_launcher_launcher_window_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
    BobLauncherLauncherWindow *self = (BobLauncherLauncherWindow *)widget;

    GskRenderNode *root = NULL;
    cairo_region_t *region = NULL;

    if (gsk_surface_data_snapshot(self->priv->surface_data, widget, &root, &region)) {
        blur_set_cairo_region(self->priv->blur, region);

        if (self->priv->client_side_shadow) {
            GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(self));
            gdk_surface_set_input_region(surface, region);
        }

        layershell_adjust_margins(GTK_WINDOW(self),
            gtk_widget_get_width(widget), gtk_widget_get_height(widget));
    }

    if (root != NULL) {
        gtk_snapshot_append_node(snapshot, root);
        gsk_render_node_unref(root);
    }
}

static void
bob_launcher_launcher_window_hide(GtkWidget *widget)
{
    BobLauncherLauncherWindow *self = BOB_LAUNCHER_LAUNCHER_WINDOW(widget);

    if (self->priv->surf != NULL)
        gdk_toplevel_restore_system_shortcuts(GDK_TOPLEVEL(self->priv->surf));

    GTK_WIDGET_CLASS(bob_launcher_launcher_window_parent_class)->hide(widget);

    state_reset();
    controller_reset();
}

static void
bob_launcher_launcher_window_show(GtkWidget *widget)
{
    BobLauncherLauncherWindow *self = (BobLauncherLauncherWindow*)widget;
    BobLauncherLauncherWindowPrivate *priv = self->priv;

    if (!gtk_widget_get_realized(widget)) {
        GTK_WIDGET_CLASS(bob_launcher_launcher_window_parent_class)->show(widget);
        bob_launcher_launcher_window_ensure_surface(self);
        if (priv->inhibit_system_shortcuts && priv->surf != NULL)
            gdk_toplevel_inhibit_system_shortcuts(GDK_TOPLEVEL(priv->surf), NULL);
        if (priv->surf != NULL)
            gdk_wayland_toplevel_set_application_id(priv->surf, BOB_LAUNCHER_APP_ID);
    } else {
        if (priv->inhibit_system_shortcuts && priv->surf != NULL) {
            gdk_toplevel_inhibit_system_shortcuts(GDK_TOPLEVEL(priv->surf), NULL);
        }
        if (priv->surf != NULL) {
            gdk_wayland_toplevel_set_application_id(priv->surf, BOB_LAUNCHER_APP_ID);
        }
        GTK_WIDGET_CLASS(bob_launcher_launcher_window_parent_class)->show(widget);
    }
}

/* ============================================================================
 * GObject property accessors
 * ============================================================================ */

static void
bob_launcher_launcher_window_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    BobLauncherLauncherWindow *self = (BobLauncherLauncherWindow*)object;

    switch (prop_id) {
        case PROP_CLIENT_SIDE_SHADOW:
            g_value_set_boolean(value, self->priv->client_side_shadow);
            break;
        case PROP_INHIBIT_SYSTEM_SHORTCUTS:
            g_value_set_boolean(value, self->priv->inhibit_system_shortcuts);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void
bob_launcher_launcher_window_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    BobLauncherLauncherWindow *self = BOB_LAUNCHER_LAUNCHER_WINDOW(object);

    switch (prop_id) {
        case PROP_CLIENT_SIDE_SHADOW:
            self->priv->client_side_shadow = g_value_get_boolean(value);
            break;
        case PROP_INHIBIT_SYSTEM_SHORTCUTS:
            self->priv->inhibit_system_shortcuts = g_value_get_boolean(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

/* ============================================================================
 * GObject lifecycle
 * ============================================================================ */

static void
bob_launcher_launcher_window_dispose(GObject *obj)
{
    BobLauncherLauncherWindow *self = BOB_LAUNCHER_LAUNCHER_WINDOW(obj);
    blur_surface_destroy(self->priv->blur);
    gsk_surface_data_destroy(self->priv->surface_data);

    if (bob_launcher_launcher_window_up_down_handle != NULL) {
        gtk_widget_unparent(GTK_WIDGET(bob_launcher_launcher_window_up_down_handle));
        bob_launcher_launcher_window_up_down_handle = NULL;
    }
    if (bob_launcher_launcher_window_width_handle != NULL) {
        gtk_widget_unparent(GTK_WIDGET(bob_launcher_launcher_window_width_handle));
        bob_launcher_launcher_window_width_handle = NULL;
    }

    g_clear_object(&self->priv->settings);

    G_OBJECT_CLASS(bob_launcher_launcher_window_parent_class)->dispose(obj);
}

static void
bob_launcher_launcher_window_class_init(BobLauncherLauncherWindowClass *klass, gpointer klass_data)
{
    bob_launcher_launcher_window_parent_class = g_type_class_peek_parent(klass);
    g_type_class_adjust_private_offset(klass, &BobLauncherLauncherWindow_private_offset);

    GObjectClass *obj_class = G_OBJECT_CLASS(klass);
    obj_class->dispose = bob_launcher_launcher_window_dispose;
    obj_class->get_property = bob_launcher_launcher_window_get_property;
    obj_class->set_property = bob_launcher_launcher_window_set_property;

    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
    widget_class->size_allocate = bob_launcher_launcher_window_size_allocate;
    widget_class->snapshot = bob_launcher_launcher_window_snapshot;
    widget_class->hide = bob_launcher_launcher_window_hide;
    widget_class->show = bob_launcher_launcher_window_show;

    properties[PROP_CLIENT_SIDE_SHADOW] =
        g_param_spec_boolean("client-side-shadow", NULL, NULL, FALSE, G_PARAM_READWRITE);
    properties[PROP_INHIBIT_SYSTEM_SHORTCUTS] =
        g_param_spec_boolean("inhibit-system-shortcuts", NULL, NULL, FALSE, G_PARAM_READWRITE);

    g_object_class_install_properties(obj_class, N_PROPERTIES, properties);

    /* Initialize static class variables */
    launcher_window_appsettings = bob_launcher_app_settings_get_default();
}

static void on_color_scheme_changed(GSettings *settings, const char *key, gpointer user_data) {
    char *scheme = g_settings_get_string(settings, "color-scheme");
    // GtkInterfaceColorScheme: 0=UNSUPPORTED, 1=DEFAULT, 2=DARK, 3=LIGHT
    int gtk_scheme = strcmp(scheme, "prefer-dark") == 0 ? 2 :
                     strcmp(scheme, "prefer-light") == 0 ? 3 : 1;
    g_object_set(gtk_settings_get_default(), "gtk-interface-color-scheme", gtk_scheme, NULL);
    g_free(scheme);
}

static void
bob_launcher_launcher_window_instance_init(BobLauncherLauncherWindow *self, gpointer klass)
{
    self->priv = bob_launcher_launcher_window_get_instance_private(self);
    BobLauncherLauncherWindowPrivate *priv = self->priv;

    priv->surf = NULL;
    priv->client_side_shadow = FALSE;
    priv->inhibit_system_shortcuts = FALSE;
    priv->current_monitor = NULL;
    priv->surface_data = gsk_surface_data_new(BOB_LAUNCHER_TYPE_LAUNCHER_WINDOW);

    priv->settings = g_settings_new(BOB_LAUNCHER_APP_ID ".ui");

    g_settings_bind(priv->settings, "client-side-shadow", self, "client-side-shadow", G_SETTINGS_BIND_GET);
    g_signal_connect_after(priv->settings, "changed::client-side-shadow",
                           G_CALLBACK(on_shadow_settings_changed), self);
    handle_shadow_settings(self);

    GSettings *desktop_settings = g_settings_new("org.gnome.desktop.interface");
    g_signal_connect(desktop_settings, "changed::color-scheme",
                     G_CALLBACK(on_color_scheme_changed), NULL);

    on_color_scheme_changed(desktop_settings, "color-scheme", NULL);

    g_settings_bind(priv->settings, "inhibit-system-shortcuts", self, "inhibit-system-shortcuts", G_SETTINGS_BIND_GET);
    g_signal_connect_after(priv->settings, "changed::inhibit-system-shortcuts",
                           G_CALLBACK(on_inhibit_settings_changed), self);

    g_signal_connect(priv->settings, "changed::client-side-border",
                     G_CALLBACK(on_border_settings_changed), self);
    handle_border_settings(self);

    gtk_widget_set_can_focus(GTK_WIDGET(self), FALSE);
    gtk_widget_set_focusable(GTK_WIDGET(self), FALSE);
    g_object_set(self,
                 "deletable", FALSE,
                 "resizable", FALSE,
                 "handle-menubar-accel", FALSE,
                 "title", "BobLauncher",
                 NULL);
    gtk_widget_set_name(GTK_WIDGET(self), "launcher");

    gtk_window_set_child(GTK_WINDOW(self), GTK_WIDGET(bob_launcher_main_container_new()));

    bob_launcher_launcher_window_up_down_handle = bob_launcher_up_down_resize_handle_new();
    gtk_widget_set_parent(GTK_WIDGET(bob_launcher_launcher_window_up_down_handle), GTK_WIDGET(self));

    bob_launcher_launcher_window_width_handle = bob_launcher_width_resize_handle_new();
    gtk_widget_set_parent(GTK_WIDGET(bob_launcher_launcher_window_width_handle), GTK_WIDGET(self));

    gtk_window_set_default_size(GTK_WINDOW(self), 1, 1);

    layershell_setup(GTK_WINDOW(self), launcher_window_appsettings);
    disable_controllers(GTK_WIDGET(self));
}

/* ============================================================================
 * Type registration
 * ============================================================================ */

static GType
bob_launcher_launcher_window_get_type_once(void)
{
    static const GTypeInfo info = {
        sizeof(BobLauncherLauncherWindowClass),
        NULL, NULL,
        (GClassInitFunc)bob_launcher_launcher_window_class_init,
        NULL, NULL,
        sizeof(BobLauncherLauncherWindow),
        0,
        (GInstanceInitFunc)bob_launcher_launcher_window_instance_init,
        NULL
    };

    GType type_id = g_type_register_static(GTK_TYPE_WINDOW, "BobLauncherLauncherWindow", &info, 0);
    BobLauncherLauncherWindow_private_offset = g_type_add_instance_private(type_id, sizeof(BobLauncherLauncherWindowPrivate));
    return type_id;
}

GType
bob_launcher_launcher_window_get_type(void)
{
    static volatile gsize type_id_once = 0;
    if (g_once_init_enter(&type_id_once)) {
        GType type_id = bob_launcher_launcher_window_get_type_once();
        g_once_init_leave(&type_id_once, type_id);
    }
    return type_id_once;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

BobLauncherLauncherWindow *
bob_launcher_launcher_window_new(void)
{
    return g_object_new(BOB_LAUNCHER_TYPE_LAUNCHER_WINDOW, NULL);
}

void
bob_launcher_launcher_window_ensure_surface(BobLauncherLauncherWindow *self)
{
    GdkSurface *mysurf = gtk_native_get_surface(GTK_NATIVE(self));
    g_signal_connect_after(mysurf, "enter-monitor", G_CALLBACK(on_monitor_changed), self);

    self->priv->surf = GDK_WAYLAND_TOPLEVEL(gtk_native_get_surface(GTK_NATIVE(self)));

    struct wl_surface *wayland_surface = gdk_wayland_surface_get_wl_surface(GDK_WAYLAND_SURFACE(mysurf));

    keyboard_add_surface(bob_launcher_app_keyboard, wayland_surface,
                         controller_handle_key_press,
                         controller_handle_key_release,
                         controller_handle_focus_enter,
                         controller_handle_focus_leave,
                         controller_handle_compose, NULL);

    self->priv->blur = blur_surface_create(wayland_surface);
}

void
bob_launcher_launcher_window_reset_surface_data(BobLauncherLauncherWindow *self)
{
    gsk_surface_data_reset(self->priv->surface_data);
}

