#include "text-image.h"
#include "bob-launcher.h"
#include <icon-cache-service.h>
#include <graphene-gobject.h>

struct _BobLauncherTextImage {
    GtkWidget parent_instance;
    gchar *icon_name;
};

struct _BobLauncherTextImageClass {
    GtkWidgetClass parent_class;
};

static gpointer bob_launcher_text_image_parent_class = NULL;

static void bob_launcher_text_image_snapshot(GtkWidget *widget, GtkSnapshot *snapshot);
static void bob_launcher_text_image_finalize(GObject *obj);

static void
bob_launcher_text_image_class_init(BobLauncherTextImageClass *klass, gpointer klass_data)
{
    bob_launcher_text_image_parent_class = g_type_class_peek_parent(klass);

    G_OBJECT_CLASS(klass)->finalize = bob_launcher_text_image_finalize;
    GTK_WIDGET_CLASS(klass)->snapshot = bob_launcher_text_image_snapshot;

    gtk_widget_class_set_css_name(GTK_WIDGET_CLASS(klass), "text-image");
}

static void
bob_launcher_text_image_instance_init(BobLauncherTextImage *self, gpointer klass)
{
    gtk_widget_set_overflow(GTK_WIDGET(self), GTK_OVERFLOW_HIDDEN);
    gtk_widget_set_can_target(GTK_WIDGET(self), TRUE);
    gtk_widget_set_valign(GTK_WIDGET(self), GTK_ALIGN_BASELINE_FILL);
    gtk_widget_set_vexpand(GTK_WIDGET(self), TRUE);

    const gchar *css_classes[] = { "fragment", NULL };
    gtk_widget_set_css_classes(GTK_WIDGET(self), css_classes);
}

static void
bob_launcher_text_image_finalize(GObject *obj)
{
    BobLauncherTextImage *self = BOB_LAUNCHER_TEXT_IMAGE(obj);

    g_free(self->icon_name);

    G_OBJECT_CLASS(bob_launcher_text_image_parent_class)->finalize(obj);
}

void
bob_launcher_text_image_update_icon_name(BobLauncherTextImage *self, const gchar *new_icon_name)
{
    g_return_if_fail(BOB_LAUNCHER_IS_TEXT_IMAGE(self));
    g_return_if_fail(new_icon_name != NULL);

    if (g_strcmp0(self->icon_name, new_icon_name) != 0) {
        g_free(self->icon_name);
        self->icon_name = g_strdup(new_icon_name);
        gtk_widget_queue_draw(GTK_WIDGET(self));
    }
}

static void
bob_launcher_text_image_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
    BobLauncherTextImage *self = BOB_LAUNCHER_TEXT_IMAGE(widget);

    const gint width = gtk_widget_get_width(widget);
    const gint height = gtk_widget_get_height(widget);
    const gint shortest = MIN(width, height);

    if (shortest <= 0)
        return;

    gfloat shift_x = 0.0f;
    gfloat shift_y = 0.0f;

    if (width > height) {
        shift_x = (gfloat)(width - height) / 2.0f;
    } else if (height > width) {
        shift_y = (gfloat)(height - width) / 2.0f;
    }

    GdkRGBA color;
    gtk_widget_get_color(widget, &color);

    /* Stack-allocated graphene types - no heap allocation needed */
    graphene_matrix_t color_matrix;
    const float matrix_values[16] = {
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, color.alpha
    };
    graphene_matrix_init_from_float(&color_matrix, matrix_values);

    graphene_vec4_t color_offset;
    graphene_vec4_init(&color_offset, color.red, color.green, color.blue, 0.0f);

    gtk_snapshot_push_color_matrix(snapshot, &color_matrix, &color_offset);

    GdkPaintable *paintable = icon_cache_service_get_paintable_for_icon_name(
        self->icon_name,
        shortest,
        gtk_widget_get_scale_factor(widget)
    );

    if (shift_x != 0.0f || shift_y != 0.0f) {
        graphene_point_t offset = GRAPHENE_POINT_INIT(shift_x, shift_y);
        gtk_snapshot_translate(snapshot, &offset);
    }

    gdk_paintable_snapshot(paintable, GDK_SNAPSHOT(snapshot), (gdouble)shortest, (gdouble)shortest);

    gtk_snapshot_pop(snapshot);
}

static GType
bob_launcher_text_image_get_type_once(void)
{
    static const GTypeInfo info = {
        sizeof(BobLauncherTextImageClass),
        NULL, NULL,
        (GClassInitFunc)bob_launcher_text_image_class_init,
        NULL, NULL,
        sizeof(BobLauncherTextImage),
        0,
        (GInstanceInitFunc)bob_launcher_text_image_instance_init,
        NULL
    };
    return g_type_register_static(GTK_TYPE_WIDGET, "BobLauncherTextImage", &info, 0);
}

GType
bob_launcher_text_image_get_type(void)
{
    static volatile gsize type_id__once = 0;
    if (g_once_init_enter(&type_id__once)) {
        GType type_id = bob_launcher_text_image_get_type_once();
        g_once_init_leave(&type_id__once, type_id);
    }
    return type_id__once;
}

BobLauncherTextImage *
bob_launcher_text_image_new(void)
{
    return g_object_new(BOB_LAUNCHER_TYPE_TEXT_IMAGE, NULL);
}
