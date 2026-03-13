#include "fast-label.h"

struct _BobLauncherFastLabel {
    GtkWidget parent_instance;
    PangoLayout *layout;
    int cached_width;
    int cached_height;
    int cached_baseline;
    bool needs_measure;
};

struct _BobLauncherFastLabelClass {
    GtkWidgetClass parent_class;
};

static gpointer bob_launcher_fast_label_parent_class = NULL;

static void
bob_launcher_fast_label_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
    BobLauncherFastLabel *self = BOB_LAUNCHER_FAST_LABEL(widget);

    if (self->layout == NULL)
        return;

    GdkRGBA color;
    gtk_widget_get_color(widget, &color);

    /* Adjust y position to align layout baseline with widget's allocated baseline */
    int allocated_baseline = gtk_widget_get_baseline(widget);
    if (allocated_baseline != -1) {
        float y = allocated_baseline - self->cached_baseline;
        gtk_snapshot_translate(snapshot, &GRAPHENE_POINT_INIT(0, y));
    }

    gtk_snapshot_append_layout(snapshot, self->layout, &color);
}

static void
bob_launcher_fast_label_measure(GtkWidget *widget,
                                 GtkOrientation orientation,
                                 int for_size,
                                 int *minimum,
                                 int *natural,
                                 int *minimum_baseline,
                                 int *natural_baseline)
{
    BobLauncherFastLabel *self = BOB_LAUNCHER_FAST_LABEL(widget);

    if (self->needs_measure) {
        int width, height;
        pango_layout_get_size(self->layout, &width, &height);
        self->cached_width = PANGO_PIXELS_CEIL(width);
        self->cached_height = PANGO_PIXELS_CEIL(height);
        self->cached_baseline = pango_layout_get_baseline(self->layout) / PANGO_SCALE;
        self->needs_measure = false;
    }

    if (orientation == GTK_ORIENTATION_HORIZONTAL) {
        *minimum = *natural = self->cached_width;
        *minimum_baseline = *natural_baseline = -1;
    } else {
        *minimum = *natural = self->cached_height;
        *minimum_baseline = *natural_baseline = self->cached_baseline;
    }
}

static GtkSizeRequestMode
bob_launcher_fast_label_get_request_mode(GtkWidget *widget)
{
    return GTK_SIZE_REQUEST_CONSTANT_SIZE;
}

static void
bob_launcher_fast_label_finalize(GObject *object)
{
    BobLauncherFastLabel *self = BOB_LAUNCHER_FAST_LABEL(object);
    g_clear_object(&self->layout);
    G_OBJECT_CLASS(bob_launcher_fast_label_parent_class)->finalize(object);
}

static void
bob_launcher_fast_label_css_changed(GtkWidget *widget, GtkCssStyleChange *change)
{
    BobLauncherFastLabel *self = BOB_LAUNCHER_FAST_LABEL(widget);
    self->needs_measure = true;
    pango_layout_context_changed(self->layout);
    gtk_widget_queue_resize(GTK_WIDGET(self));

    GTK_WIDGET_CLASS(bob_launcher_fast_label_parent_class)->css_changed(widget, change);
}

static void
bob_launcher_fast_label_class_init(BobLauncherFastLabelClass *klass, gpointer klass_data)
{
    bob_launcher_fast_label_parent_class = g_type_class_peek_parent(klass);

    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = bob_launcher_fast_label_finalize;

    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
    widget_class->snapshot = bob_launcher_fast_label_snapshot;
    widget_class->measure = bob_launcher_fast_label_measure;
    widget_class->get_request_mode = bob_launcher_fast_label_get_request_mode;
    widget_class->css_changed = bob_launcher_fast_label_css_changed;

    gtk_widget_class_set_css_name(widget_class, "label");
}

static void
bob_launcher_fast_label_instance_init(BobLauncherFastLabel *self, gpointer klass)
{
    self->layout = NULL;
    self->cached_width = 0;
    self->cached_height = 0;
    self->cached_baseline = 0;
    self->needs_measure = false;

    gtk_widget_set_overflow(GTK_WIDGET(self), GTK_OVERFLOW_VISIBLE);

    PangoContext *pango_ctx = gtk_widget_get_pango_context(GTK_WIDGET(self));
    self->layout = pango_layout_new(pango_ctx);
    pango_layout_set_single_paragraph_mode(self->layout, TRUE);
}

static GType
bob_launcher_fast_label_get_type_once(void)
{
    static const GTypeInfo info = {
        sizeof(BobLauncherFastLabelClass),
        NULL, NULL,
        (GClassInitFunc) bob_launcher_fast_label_class_init,
        NULL, NULL,
        sizeof(BobLauncherFastLabel),
        0,
        (GInstanceInitFunc) bob_launcher_fast_label_instance_init,
        NULL
    };
    return g_type_register_static(GTK_TYPE_WIDGET, "BobLauncherFastLabel", &info, 0);
}

GType
bob_launcher_fast_label_get_type(void)
{
    static gsize type_id = 0;
    if (g_once_init_enter(&type_id)) {
        GType id = bob_launcher_fast_label_get_type_once();
        g_once_init_leave(&type_id, id);
    }
    return type_id;
}

BobLauncherFastLabel*
bob_launcher_fast_label_new(void)
{
    return g_object_new(BOB_LAUNCHER_TYPE_FAST_LABEL, NULL);
}

void
bob_launcher_fast_label_set_text(BobLauncherFastLabel *self,
                                  const char *text,
                                  PangoAttrList *attrs)
{
    PangoAttrList *old_attrs = pango_layout_get_attributes(self->layout);
    bool attrs_changed = !pango_attr_list_equal(old_attrs, attrs);

    if (attrs_changed)
        pango_layout_set_attributes(self->layout, attrs);

    const char *old_text = pango_layout_get_text(self->layout);
    self->needs_measure = self->needs_measure || strcmp(text, old_text) != 0;

    if (self->needs_measure) {
        pango_layout_set_text(self->layout, text, -1);
        gtk_widget_queue_resize(GTK_WIDGET(self));
    } else if (attrs_changed) {
        gtk_widget_queue_draw(GTK_WIDGET(self));
    }
}

void
bob_launcher_fast_label_invalidate(BobLauncherFastLabel *self)
{
    self->needs_measure = true;
    pango_layout_context_changed(self->layout);
    gtk_widget_queue_resize(GTK_WIDGET(self));
}
