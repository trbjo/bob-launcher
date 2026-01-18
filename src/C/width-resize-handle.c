#include "width-resize-handle.h"
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <stdio.h>

#define BOB_LAUNCHER_APP_ID "io.github.trbjo.bob.launcher"

struct _BobLauncherWidthResizeHandle {
    GtkWidget parent_instance;
    GdkCursor *pointer;
    GtkGestureDrag *drag_gesture;
};

struct _BobLauncherWidthResizeHandleClass {
    GtkWidgetClass parent_class;
};

static gpointer bob_launcher_width_resize_handle_parent_class = NULL;
static GtkCssProvider *css_provider = NULL;
static GSettings *ui_settings = NULL;

static void
get_setting_range(GSettings *settings, const char *key, int *min, int *max)
{
    GSettingsSchema *schema;
    g_object_get(settings, "settings-schema", &schema, NULL);

    GSettingsSchemaKey *schema_key = g_settings_schema_get_key(schema, key);
    GVariant *range = g_settings_schema_key_get_range(schema_key);

    GVariant *range_variant;
    g_variant_get(range, "(sv)", NULL, &range_variant);
    g_variant_get(range_variant, "(ii)", min, max);

    g_variant_unref(range_variant);
    g_variant_unref(range);
    g_settings_schema_key_unref(schema_key);
    g_settings_schema_unref(schema);
}

static void
css_set_width(int width)
{
    GdkDisplay *display = gdk_display_get_default();
    gtk_style_context_remove_provider_for_display(display, GTK_STYLE_PROVIDER(css_provider));

    char css[128];
    snprintf(css, sizeof(css), "#main-container { min-width: %dpx; }", width);
    gtk_css_provider_load_from_string(css_provider, css);

    gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(css_provider),
                                                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static void
on_drag_update(GtkGestureDrag *gesture, double x, double y, gpointer user_data)
{
    (void)gesture;
    (void)y;
    (void)user_data;

    int current = g_settings_get_int(ui_settings, "width");
    int new_value = current + (int)x;

    int min, max;
    get_setting_range(ui_settings, "width", &min, &max);
    new_value = MAX(min, MIN(max, new_value));

    if (new_value != current) {
        g_settings_set_int(ui_settings, "width", new_value);
    }
}

static void
on_state_flags_changed(GtkWidget *widget, GtkStateFlags old_flags, gpointer user_data)
{
    (void)old_flags;
    (void)user_data;

    BobLauncherWidthResizeHandle *self = BOB_LAUNCHER_WIDTH_RESIZE_HANDLE(widget);

    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(self->drag_gesture), GTK_PHASE_BUBBLE);

    GtkStateFlags new_flags = gtk_widget_get_state_flags(widget);
    gboolean is_hover = (new_flags & GTK_STATE_FLAG_PRELIGHT) != 0;
    if (is_hover) {
        gtk_widget_set_cursor(widget, self->pointer);
    }
}

static void
on_width_changed(GSettings *settings, const char *key, gpointer user_data)
{
    (void)settings;
    (void)key;
    (void)user_data;

    css_set_width(g_settings_get_int(ui_settings, "width"));
}

static void
bob_launcher_width_resize_handle_finalize(GObject *obj)
{
    BobLauncherWidthResizeHandle *self = BOB_LAUNCHER_WIDTH_RESIZE_HANDLE(obj);

    g_clear_object(&self->pointer);
    /* Note: drag_gesture is owned by the widget (via add_controller), don't unref */

    G_OBJECT_CLASS(bob_launcher_width_resize_handle_parent_class)->finalize(obj);
}

static void
bob_launcher_width_resize_handle_class_init(BobLauncherWidthResizeHandleClass *klass, gpointer klass_data)
{
    (void)klass_data;

    bob_launcher_width_resize_handle_parent_class = g_type_class_peek_parent(klass);

    G_OBJECT_CLASS(klass)->finalize = bob_launcher_width_resize_handle_finalize;

    gtk_widget_class_set_css_name(GTK_WIDGET_CLASS(klass), "handle");

    css_provider = gtk_css_provider_new();
    ui_settings = g_settings_new(BOB_LAUNCHER_APP_ID ".ui");
}

static void
bob_launcher_width_resize_handle_instance_init(BobLauncherWidthResizeHandle *self, gpointer klass)
{
    (void)klass;

    const char *classes[] = { "horizontal", NULL };
    gtk_widget_set_css_classes(GTK_WIDGET(self), classes);

    self->pointer = gdk_cursor_new_from_name("ew-resize", NULL);

    self->drag_gesture = GTK_GESTURE_DRAG(gtk_gesture_drag_new());
    gtk_widget_add_controller(GTK_WIDGET(self), GTK_EVENT_CONTROLLER(self->drag_gesture));

    g_signal_connect(self->drag_gesture, "drag-update", G_CALLBACK(on_drag_update), self);
    g_signal_connect(self, "state-flags-changed", G_CALLBACK(on_state_flags_changed), NULL);

    css_set_width(g_settings_get_int(ui_settings, "width"));
    g_signal_connect(ui_settings, "changed::width", G_CALLBACK(on_width_changed), self);
}

static GType
bob_launcher_width_resize_handle_get_type_once(void)
{
    static const GTypeInfo info = {
        sizeof(BobLauncherWidthResizeHandleClass),
        NULL, NULL,
        (GClassInitFunc)bob_launcher_width_resize_handle_class_init,
        NULL, NULL,
        sizeof(BobLauncherWidthResizeHandle),
        0,
        (GInstanceInitFunc)bob_launcher_width_resize_handle_instance_init,
        NULL
    };
    return g_type_register_static(GTK_TYPE_WIDGET, "BobLauncherWidthResizeHandle", &info, 0);
}

GType
bob_launcher_width_resize_handle_get_type(void)
{
    static volatile gsize type_id__once = 0;
    if (g_once_init_enter(&type_id__once)) {
        GType type_id = bob_launcher_width_resize_handle_get_type_once();
        g_once_init_leave(&type_id__once, type_id);
    }
    return type_id__once;
}

BobLauncherWidthResizeHandle *
bob_launcher_width_resize_handle_new(void)
{
    return g_object_new(BOB_LAUNCHER_TYPE_WIDTH_RESIZE_HANDLE, NULL);
}
