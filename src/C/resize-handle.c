#include "resize-handle.h"
#include <gtk/gtk.h>
#include <gio/gio.h>

#define BOB_LAUNCHER_APP_ID "io.github.trbjo.bob.launcher"

typedef struct _BobLauncherMatchRow BobLauncherMatchRow;

extern BobLauncherMatchRow **bob_launcher_result_box_row_pool;
extern gint bob_launcher_result_box_visible_size;

struct _BobLauncherUpDownResizeHandle {
    GtkWidget parent_instance;
    GdkCursor *pointer;
    GtkGestureDrag *drag_gesture;
};

struct _BobLauncherUpDownResizeHandleClass {
    GtkWidgetClass parent_class;
};

static gpointer bob_launcher_up_down_resize_handle_parent_class = NULL;
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
on_drag_update(GtkGestureDrag *gesture, double x, double y, gpointer user_data)
{
    (void)gesture;
    (void)x;
    (void)user_data;

    int height = gtk_widget_get_height(GTK_WIDGET(bob_launcher_result_box_row_pool[0]));
    if (height == 0) return;

    int result = ((int)y) / height;
    if (result == 0) return;

    int current = g_settings_get_int(ui_settings, "box-size");
    int new_value = result + current;

    int min, max;
    get_setting_range(ui_settings, "box-size", &min, &max);
    new_value = MAX(min, MIN(max, new_value));

    if (new_value != current) {
        g_settings_set_int(ui_settings, "box-size", new_value);
    }
}

static void
on_state_flags_changed(GtkWidget *widget, GtkStateFlags old_flags, gpointer user_data)
{
    (void)old_flags;
    (void)user_data;

    BobLauncherUpDownResizeHandle *self = BOB_LAUNCHER_UP_DOWN_RESIZE_HANDLE(widget);

    if (bob_launcher_result_box_visible_size == 0) {
        gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(self->drag_gesture), GTK_PHASE_NONE);
        return;
    }

    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(self->drag_gesture), GTK_PHASE_BUBBLE);

    GtkStateFlags new_flags = gtk_widget_get_state_flags(widget);
    gboolean is_hover = (new_flags & GTK_STATE_FLAG_PRELIGHT) != 0;
    if (is_hover) {
        gtk_widget_set_cursor(widget, self->pointer);
    }
}

static void
bob_launcher_up_down_resize_handle_finalize(GObject *obj)
{
    BobLauncherUpDownResizeHandle *self = BOB_LAUNCHER_UP_DOWN_RESIZE_HANDLE(obj);

    g_clear_object(&self->pointer);
    /* Note: drag_gesture is owned by the widget (via add_controller), don't unref */

    G_OBJECT_CLASS(bob_launcher_up_down_resize_handle_parent_class)->finalize(obj);
}

static void
bob_launcher_up_down_resize_handle_class_init(BobLauncherUpDownResizeHandleClass *klass, gpointer klass_data)
{
    (void)klass_data;

    bob_launcher_up_down_resize_handle_parent_class = g_type_class_peek_parent(klass);

    G_OBJECT_CLASS(klass)->finalize = bob_launcher_up_down_resize_handle_finalize;

    gtk_widget_class_set_css_name(GTK_WIDGET_CLASS(klass), "handle");

    ui_settings = g_settings_new(BOB_LAUNCHER_APP_ID ".ui");
}

static void
bob_launcher_up_down_resize_handle_instance_init(BobLauncherUpDownResizeHandle *self, gpointer klass)
{
    (void)klass;

    const char *classes[] = { "vertical", NULL };
    gtk_widget_set_css_classes(GTK_WIDGET(self), classes);

    self->pointer = gdk_cursor_new_from_name("ns-resize", NULL);

    self->drag_gesture = GTK_GESTURE_DRAG(gtk_gesture_drag_new());
    gtk_widget_add_controller(GTK_WIDGET(self), GTK_EVENT_CONTROLLER(self->drag_gesture));

    g_signal_connect(self->drag_gesture, "drag-update", G_CALLBACK(on_drag_update), self);
    g_signal_connect(self, "state-flags-changed", G_CALLBACK(on_state_flags_changed), NULL);
}

static GType
bob_launcher_up_down_resize_handle_get_type_once(void)
{
    static const GTypeInfo info = {
        sizeof(BobLauncherUpDownResizeHandleClass),
        NULL, NULL,
        (GClassInitFunc)bob_launcher_up_down_resize_handle_class_init,
        NULL, NULL,
        sizeof(BobLauncherUpDownResizeHandle),
        0,
        (GInstanceInitFunc)bob_launcher_up_down_resize_handle_instance_init,
        NULL
    };
    return g_type_register_static(GTK_TYPE_WIDGET, "BobLauncherUpDownResizeHandle", &info, 0);
}

GType
bob_launcher_up_down_resize_handle_get_type(void)
{
    static volatile gsize type_id__once = 0;
    if (g_once_init_enter(&type_id__once)) {
        GType type_id = bob_launcher_up_down_resize_handle_get_type_once();
        g_once_init_leave(&type_id__once, type_id);
    }
    return type_id__once;
}

BobLauncherUpDownResizeHandle *
bob_launcher_up_down_resize_handle_new(void)
{
    return g_object_new(BOB_LAUNCHER_TYPE_UP_DOWN_RESIZE_HANDLE, NULL);
}
