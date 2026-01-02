#include "row-number.h"
#include <gtk/gtk.h>
#include <pango/pango.h>

struct _BobLauncherRowNumber {
    GtkWidget parent_instance;
    PangoLayout *layout;
    int width;
    int height;
    int row_num;
};

struct _BobLauncherRowNumberClass {
    GtkWidgetClass parent_class;
};

static gpointer bob_launcher_row_number_parent_class = NULL;

static void bob_launcher_row_number_measure(GtkWidget *widget, GtkOrientation orientation, int for_size,
                                             int *minimum, int *natural, int *minimum_baseline, int *natural_baseline);
static void bob_launcher_row_number_snapshot(GtkWidget *widget, GtkSnapshot *snapshot);
static void bob_launcher_row_number_finalize(GObject *obj);

static void
bob_launcher_row_number_class_init(BobLauncherRowNumberClass *klass, gpointer klass_data)
{
    (void)klass_data;

    bob_launcher_row_number_parent_class = g_type_class_peek_parent(klass);

    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
    widget_class->measure = bob_launcher_row_number_measure;
    widget_class->snapshot = bob_launcher_row_number_snapshot;

    G_OBJECT_CLASS(klass)->finalize = bob_launcher_row_number_finalize;

    gtk_widget_class_set_css_name(widget_class, "row-number");
}

static void
bob_launcher_row_number_instance_init(BobLauncherRowNumber *self, gpointer klass)
{
    (void)klass;

    self->layout = NULL;
    self->width = 0;
    self->height = 0;
    self->row_num = 65536;
}

static void
bob_launcher_row_number_finalize(GObject *obj)
{
    BobLauncherRowNumber *self = BOB_LAUNCHER_ROW_NUMBER(obj);

    g_clear_object(&self->layout);

    G_OBJECT_CLASS(bob_launcher_row_number_parent_class)->finalize(obj);
}

static GType
bob_launcher_row_number_get_type_once(void)
{
    static const GTypeInfo info = {
        sizeof(BobLauncherRowNumberClass),
        NULL, NULL,
        (GClassInitFunc)bob_launcher_row_number_class_init,
        NULL, NULL,
        sizeof(BobLauncherRowNumber),
        0,
        (GInstanceInitFunc)bob_launcher_row_number_instance_init,
        NULL
    };
    return g_type_register_static(GTK_TYPE_WIDGET, "BobLauncherRowNumber", &info, 0);
}

GType
bob_launcher_row_number_get_type(void)
{
    static volatile gsize type_id__once = 0;
    if (g_once_init_enter(&type_id__once)) {
        GType type_id = bob_launcher_row_number_get_type_once();
        g_once_init_leave(&type_id__once, type_id);
    }
    return type_id__once;
}

BobLauncherRowNumber *
bob_launcher_row_number_new(int row_num)
{
    BobLauncherRowNumber *self = g_object_new(BOB_LAUNCHER_TYPE_ROW_NUMBER, NULL);

    gtk_widget_set_halign(GTK_WIDGET(self), GTK_ALIGN_CENTER);

    const char *classes[] = { "shortcut", NULL };
    gtk_widget_set_css_classes(GTK_WIDGET(self), classes);

    self->layout = gtk_widget_create_pango_layout(GTK_WIDGET(self), NULL);
    pango_layout_set_alignment(self->layout, PANGO_ALIGN_CENTER);

    bob_launcher_row_number_update_row_num(self, row_num);

    return self;
}

void
bob_launcher_row_number_update_row_num(BobLauncherRowNumber *self, int new_row)
{
    int wrapped = (new_row + 1) % 10;
    if (self->row_num == wrapped) return;

    self->row_num = wrapped;

    char formatted[16];
    snprintf(formatted, sizeof(formatted), "%d", self->row_num);

    pango_layout_set_text(self->layout, formatted, -1);
    pango_layout_get_size(self->layout, &self->width, &self->height);
    self->width /= PANGO_SCALE;
    self->height /= PANGO_SCALE;

    gtk_widget_queue_draw(GTK_WIDGET(self));
}

static void
bob_launcher_row_number_measure(GtkWidget *widget, GtkOrientation orientation, int for_size,
                                 int *minimum, int *natural, int *minimum_baseline, int *natural_baseline)
{
    (void)for_size;

    BobLauncherRowNumber *self = BOB_LAUNCHER_ROW_NUMBER(widget);

    *minimum_baseline = *natural_baseline = -1;

    if (orientation == GTK_ORIENTATION_VERTICAL) {
        pango_layout_get_size(self->layout, NULL, &self->height);
        self->height /= PANGO_SCALE;
        *minimum = *natural = self->height;
    } else {
        pango_layout_get_size(self->layout, &self->width, NULL);
        self->width /= PANGO_SCALE;
        *minimum = *natural = self->width;
    }
}

static void
bob_launcher_row_number_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
    BobLauncherRowNumber *self = BOB_LAUNCHER_ROW_NUMBER(widget);

    int widget_width = gtk_widget_get_width(widget);
    int widget_height = gtk_widget_get_height(widget);

    graphene_point_t offset = GRAPHENE_POINT_INIT(
        (widget_width - self->width) / 2.0f,
        (widget_height - self->height) / 2.0f
    );
    gtk_snapshot_translate(snapshot, &offset);

    GdkRGBA color;
    gtk_widget_get_color(widget, &color);
    gtk_snapshot_append_layout(snapshot, self->layout, &color);
}
