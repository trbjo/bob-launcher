#include "match-row-label.h"
#include "bob-launcher.h"
#include <math.h>

typedef struct _BobLauncherMatchRowLabelPrivate BobLauncherMatchRowLabelPrivate;

struct _BobLauncherMatchRowLabel {
    GtkWidget parent_instance;
    BobLauncherMatchRowLabelPrivate *priv;
};

struct _BobLauncherMatchRowLabelClass {
    GtkWidgetClass parent_class;
};

struct _BobLauncherMatchRowLabelPrivate {
    double scroll_position;
    int children_width;
    int current_widget_index;
    double total_overhang;

    int visible_images;
    int visible_labels;
    int visible_children;

    GPtrArray *images;
    GPtrArray *labels;
    GPtrArray *child_labels;

    int *widget_lengths;
    int widget_lengths_size;
    int widget_lengths_length;

    int max_baseline;

    graphene_rect_t bounds;
    graphene_point_t start;
    GskColorStop stops[4];
    GtkEventController *scroll_controller;
    GtkWidget *next_expected_child;
};

static gint BobLauncherMatchRowLabel_private_offset;
static gpointer bob_launcher_match_row_label_parent_class = NULL;

static const float FADE_WIDTH = 48.0f;

extern BobLauncherTextImage *bob_launcher_text_image_new(void);
extern void bob_launcher_text_image_update_icon_name(BobLauncherTextImage *self, const char *new_icon_name);

static void bob_launcher_match_row_label_class_init(BobLauncherMatchRowLabelClass *klass, gpointer klass_data);
static void bob_launcher_match_row_label_instance_init(BobLauncherMatchRowLabel *self, gpointer klass);
static void bob_launcher_match_row_label_finalize(GObject *obj);
static void bob_launcher_match_row_label_measure(GtkWidget *widget, GtkOrientation orientation,
                                                  int for_size, int *minimum, int *natural,
                                                  int *minimum_baseline, int *natural_baseline);
static void bob_launcher_match_row_label_size_allocate(GtkWidget *widget, int width, int height, int baseline);
static void bob_launcher_match_row_label_snapshot(GtkWidget *widget, GtkSnapshot *snapshot);

static GtkSizeRequestMode bob_launcher_match_row_label_get_request_mode(GtkWidget *widget);

static inline gpointer
bob_launcher_match_row_label_get_instance_private(BobLauncherMatchRowLabel *self)
{
    return G_STRUCT_MEMBER_P(self, BobLauncherMatchRowLabel_private_offset);
}

static GType
bob_launcher_match_row_label_get_type_once(void)
{
    static const GTypeInfo g_define_type_info = {
        sizeof(BobLauncherMatchRowLabelClass),
        (GBaseInitFunc) NULL,
        (GBaseFinalizeFunc) NULL,
        (GClassInitFunc) bob_launcher_match_row_label_class_init,
        (GClassFinalizeFunc) NULL,
        NULL,
        sizeof(BobLauncherMatchRowLabel),
        0,
        (GInstanceInitFunc) bob_launcher_match_row_label_instance_init,
        NULL
    };
    GType bob_launcher_match_row_label_type_id;
    bob_launcher_match_row_label_type_id = g_type_register_static(
        GTK_TYPE_WIDGET,
        "BobLauncherMatchRowLabel",
        &g_define_type_info,
        0
    );
    BobLauncherMatchRowLabel_private_offset = g_type_add_instance_private(
        bob_launcher_match_row_label_type_id,
        sizeof(BobLauncherMatchRowLabelPrivate)
    );
    return bob_launcher_match_row_label_type_id;
}

GType
bob_launcher_match_row_label_get_type(void)
{
    static volatile gsize bob_launcher_match_row_label_type_id__once = 0;
    if (g_once_init_enter(&bob_launcher_match_row_label_type_id__once)) {
        GType bob_launcher_match_row_label_type_id;
        bob_launcher_match_row_label_type_id = bob_launcher_match_row_label_get_type_once();
        g_once_init_leave(&bob_launcher_match_row_label_type_id__once, bob_launcher_match_row_label_type_id);
    }
    return bob_launcher_match_row_label_type_id__once;
}

static void
update_scroll_position(BobLauncherMatchRowLabel *self, double dx)
{
    BobLauncherMatchRowLabelPrivate *priv = self->priv;
    priv->scroll_position = MAX(0, priv->scroll_position + dx);
    int our_width = gtk_widget_get_width(GTK_WIDGET(self));

    if (priv->scroll_position > priv->children_width - our_width) {
        priv->scroll_position = MIN(priv->scroll_position, priv->children_width - our_width);
        priv->total_overhang = 0;
    } else if (priv->children_width > our_width) {
        priv->scroll_position = MIN(priv->scroll_position, priv->children_width - our_width);
    } else {
        priv->total_overhang = 0;
        priv->scroll_position = 0;
    }
    gtk_widget_queue_allocate(GTK_WIDGET(self));
}

static gboolean
on_scroll(GtkEventControllerScroll *controller, double x, double y, gpointer user_data)
{
    BobLauncherMatchRowLabel *self = BOB_LAUNCHER_MATCH_ROW_LABEL(user_data);
    self->priv->total_overhang = 0;
    update_scroll_position(self, x);
    return TRUE;
}

static void
on_decelerate(GtkEventControllerScroll *controller, double x, double y, gpointer user_data)
{
    BobLauncherMatchRowLabel *self = BOB_LAUNCHER_MATCH_ROW_LABEL(user_data);
    self->priv->total_overhang = x;
    gtk_widget_queue_draw(GTK_WIDGET(self));
}

static void
bob_launcher_match_row_label_instance_init(BobLauncherMatchRowLabel *self, gpointer klass)
{
    self->priv = bob_launcher_match_row_label_get_instance_private(self);
    BobLauncherMatchRowLabelPrivate *priv = self->priv;

    gtk_widget_set_valign(GTK_WIDGET(self), GTK_ALIGN_BASELINE_FILL);
    gtk_widget_set_halign(GTK_WIDGET(self), GTK_ALIGN_START);
    gtk_widget_set_hexpand(GTK_WIDGET(self), TRUE);
    gtk_widget_set_overflow(GTK_WIDGET(self), GTK_OVERFLOW_VISIBLE);

    priv->start = GRAPHENE_POINT_INIT(0, 0);
    priv->scroll_position = 0;
    priv->total_overhang = 0;
    priv->max_baseline = 0;
    priv->next_expected_child = NULL;

    priv->scroll_controller = gtk_event_controller_scroll_new(
        GTK_EVENT_CONTROLLER_SCROLL_HORIZONTAL | GTK_EVENT_CONTROLLER_SCROLL_KINETIC);
    gtk_widget_add_controller(GTK_WIDGET(self), priv->scroll_controller);
    g_signal_connect(priv->scroll_controller, "decelerate", G_CALLBACK(on_decelerate), self);
    g_signal_connect(priv->scroll_controller, "scroll", G_CALLBACK(on_scroll), self);

    priv->images = g_ptr_array_new_with_free_func(g_object_unref);
    priv->labels = g_ptr_array_new_with_free_func(g_object_unref);
    priv->child_labels = g_ptr_array_new_with_free_func(g_object_unref);

    priv->widget_lengths = NULL;
    priv->widget_lengths_size = 0;
    priv->widget_lengths_length = 0;
}

static void
bob_launcher_match_row_label_finalize(GObject *object)
{
    BobLauncherMatchRowLabel *self = BOB_LAUNCHER_MATCH_ROW_LABEL(object);
    BobLauncherMatchRowLabelPrivate *priv = self->priv;

    g_clear_pointer(&priv->images, g_ptr_array_unref);
    g_clear_pointer(&priv->labels, g_ptr_array_unref);
    g_clear_pointer(&priv->child_labels, g_ptr_array_unref);
    g_clear_pointer(&priv->widget_lengths, g_free);

    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(self))) != NULL) {
        gtk_widget_unparent(child);
    }

    G_OBJECT_CLASS(bob_launcher_match_row_label_parent_class)->finalize(object);
}

static void
bob_launcher_match_row_label_class_init(BobLauncherMatchRowLabelClass *klass, gpointer klass_data)
{
    bob_launcher_match_row_label_parent_class = g_type_class_peek_parent(klass);
    g_type_class_adjust_private_offset(klass, &BobLauncherMatchRowLabel_private_offset);

    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    object_class->finalize = bob_launcher_match_row_label_finalize;

    widget_class->measure = bob_launcher_match_row_label_measure;
    widget_class->size_allocate = bob_launcher_match_row_label_size_allocate;
    widget_class->snapshot = bob_launcher_match_row_label_snapshot;
    widget_class->get_request_mode = bob_launcher_match_row_label_get_request_mode;

    gtk_widget_class_set_css_name(widget_class, "match-row-label");
}

static void
ensure_widget_lengths_capacity(BobLauncherMatchRowLabelPrivate *priv, int needed)
{
    if (needed > priv->widget_lengths_size) {
        int new_size = MAX(needed, priv->widget_lengths_size * 2);
        if (new_size < 16) new_size = 16;
        priv->widget_lengths = g_realloc_n(priv->widget_lengths, new_size, sizeof(int));
        priv->widget_lengths_size = new_size;
    }
}

static void
reset(BobLauncherMatchRowLabel *self)
{
    BobLauncherMatchRowLabelPrivate *priv = self->priv;
    priv->current_widget_index = 0;
    priv->scroll_position = 0;
    priv->total_overhang = 0;
    priv->visible_images = 0;
    priv->visible_labels = 0;
    priv->visible_children = 0;
    priv->next_expected_child = gtk_widget_get_first_child(GTK_WIDGET(self));
}

static BobLauncherTextImage *
acquire_image(BobLauncherMatchRowLabel *self)
{
    BobLauncherMatchRowLabelPrivate *priv = self->priv;
    BobLauncherTextImage *icon;
    if (priv->visible_images < (int)priv->images->len) {
        icon = g_ptr_array_index(priv->images, priv->visible_images);
    } else {
        icon = bob_launcher_text_image_new();
        g_object_ref_sink(icon);
        g_ptr_array_add(priv->images, icon);
    }
    priv->visible_images++;
    return icon;
}

static GtkLabel *
acquire_label(BobLauncherMatchRowLabel *self)
{
    BobLauncherMatchRowLabelPrivate *priv = self->priv;
    GtkLabel *label;
    if (priv->visible_labels < (int)priv->labels->len) {
        label = g_ptr_array_index(priv->labels, priv->visible_labels);
    } else {
        label = GTK_LABEL(gtk_label_new(""));
        gtk_label_set_xalign(label, 0.0f);
        gtk_label_set_single_line_mode(label, TRUE);
        gtk_widget_set_hexpand(GTK_WIDGET(label), TRUE);
        gtk_widget_set_valign(GTK_WIDGET(label), GTK_ALIGN_BASELINE_CENTER);
        gtk_widget_set_vexpand(GTK_WIDGET(label), TRUE);
        gtk_widget_set_overflow(GTK_WIDGET(label), GTK_OVERFLOW_VISIBLE);
        g_object_ref_sink(label);
        g_ptr_array_add(priv->labels, label);
    }
    priv->visible_labels++;
    return label;
}

static BobLauncherMatchRowLabel *
acquire_child_label(BobLauncherMatchRowLabel *self)
{
    BobLauncherMatchRowLabelPrivate *priv = self->priv;
    BobLauncherMatchRowLabel *child;
    if (priv->visible_children < (int)priv->child_labels->len) {
        child = g_ptr_array_index(priv->child_labels, priv->visible_children);
        reset(child);
        /* No longer unparenting children here - reset() handles position tracking */
    } else {
        child = g_object_new(BOB_LAUNCHER_TYPE_MATCH_ROW_LABEL, NULL);

        /* Remove scroll controller from nested labels */
        if (child->priv->scroll_controller != NULL) {
            gtk_widget_remove_controller(GTK_WIDGET(child), child->priv->scroll_controller);
            child->priv->scroll_controller = NULL;
        }

        g_object_ref_sink(child);
        g_ptr_array_add(priv->child_labels, child);
    }
    priv->visible_children++;
    return child;
}

static void
add_widget_to_self(BobLauncherMatchRowLabel *self, GtkWidget *widget)
{
    BobLauncherMatchRowLabelPrivate *priv = self->priv;
    ensure_widget_lengths_capacity(priv, priv->current_widget_index + 1);

    GtkWidget *current_parent = gtk_widget_get_parent(widget);

    if (current_parent == GTK_WIDGET(self)) {
        /* Already our child - check if it's in the right position */
        if (widget == priv->next_expected_child) {
            /* Already in correct position - just advance the cursor */
            priv->next_expected_child = gtk_widget_get_next_sibling(widget);
        } else {
            /* Wrong position - move it before next_expected_child (or to end if NULL) */
            gtk_widget_insert_before(widget, GTK_WIDGET(self), priv->next_expected_child);
        }
    } else {
        /* Widget from different parent or not parented */
        if (current_parent != NULL) {
            gtk_widget_unparent(widget);
        }
        /* Insert before next_expected_child (or at end if NULL) */
        gtk_widget_insert_before(widget, GTK_WIDGET(self), priv->next_expected_child);
    }

    priv->current_widget_index++;
}

static void hide_unused_widgets(BobLauncherMatchRowLabel *self) {
    BobLauncherMatchRowLabelPrivate *priv = self->priv;
    for (guint i = priv->visible_labels; i < priv->labels->len; i++) {
        gtk_widget_set_visible(g_ptr_array_index(priv->labels, i), FALSE);
    }
    for (guint i = priv->visible_images; i < priv->images->len; i++) {
        gtk_widget_set_visible(g_ptr_array_index(priv->images, i), FALSE);
    }
    for (guint i = priv->visible_children; i < priv->child_labels->len; i++) {
        gtk_widget_set_visible(g_ptr_array_index(priv->child_labels, i), FALSE);
    }
}

static void
set_widget_css_class(GtkWidget *widget, const char *css_class)
{
    const char *classes[] = { css_class, NULL };
    gtk_widget_set_css_classes(widget, classes);
}

static void
process_node(BobLauncherMatchRowLabel *self, BobLauncherDescription *desc)
{
    BobLauncherFragmentType ftype = desc->fragment_type;
    const char *text = desc->text;
    const char *css_class = desc->css_class;
    PangoAttrList *attributes = desc->attributes;
    GPtrArray *children = desc->children;
    gboolean has_func = (desc->fragment_func != NULL);

    switch (ftype) {
    case BOB_LAUNCHER_FRAGMENT_TYPE_IMAGE: {
        BobLauncherTextImage *icon = acquire_image(self);
        bob_launcher_text_image_update_icon_name(icon, text);
        g_object_set_data(G_OBJECT(icon), "fragment", NULL);
        set_widget_css_class(GTK_WIDGET(icon), css_class);

        if (has_func) {
            g_object_set_data_full(G_OBJECT(icon), "fragment", g_object_ref(desc), g_object_unref);
            gtk_widget_add_css_class(GTK_WIDGET(icon), "clickable");
            gtk_widget_set_can_target(GTK_WIDGET(icon), TRUE);
        } else {
            gtk_widget_set_can_target(GTK_WIDGET(icon), FALSE);
        }
        gtk_widget_set_visible(GTK_WIDGET(icon), TRUE);
        add_widget_to_self(self, GTK_WIDGET(icon));
        break;
    }

    case BOB_LAUNCHER_FRAGMENT_TYPE_TEXT: {
        GtkLabel *label = acquire_label(self);
        set_widget_css_class(GTK_WIDGET(label), css_class);
        gtk_label_set_text(label, text);
        gtk_label_set_attributes(label, attributes);
        g_object_set_data(G_OBJECT(label), "fragment", NULL);

        if (has_func) {
            g_object_set_data_full(G_OBJECT(label), "fragment", g_object_ref(desc), g_object_unref);
            gtk_widget_add_css_class(GTK_WIDGET(label), "clickable");
            gtk_widget_set_can_target(GTK_WIDGET(label), TRUE);
        } else {
            gtk_widget_set_can_target(GTK_WIDGET(label), FALSE);
        }
        gtk_widget_set_visible(GTK_WIDGET(label), TRUE);
        add_widget_to_self(self, GTK_WIDGET(label));
        break;
    }

    case BOB_LAUNCHER_FRAGMENT_TYPE_CONTAINER: {
        BobLauncherMatchRowLabel *child = acquire_child_label(self);
        set_widget_css_class(GTK_WIDGET(child), css_class);
        g_object_set_data(G_OBJECT(child), "fragment", NULL);

        if (has_func) {
            g_object_set_data_full(G_OBJECT(child), "fragment", g_object_ref(desc), g_object_unref);
            gtk_widget_add_css_class(GTK_WIDGET(child), "clickable");
        }
        gtk_widget_set_visible(GTK_WIDGET(child), TRUE);

        /* Recursively process children into the child label */
        if (children != NULL) {
            for (guint i = 0; i < children->len; i++) {
                BobLauncherDescription *child_desc = g_ptr_array_index(children, i);
                process_node(child, child_desc);
            }
        }
        hide_unused_widgets(child);

        add_widget_to_self(self, GTK_WIDGET(child));
        break;
    }
    }
}

void
bob_launcher_match_row_label_set_text(BobLauncherMatchRowLabel *self,
                                       const char *text,
                                       PangoAttrList *attrs)
{
    g_return_if_fail(BOB_LAUNCHER_IS_MATCH_ROW_LABEL(self));

    reset(self);
    /* No longer unparenting - widgets stay parented, we just reuse them */

    GtkLabel *label = acquire_label(self);
    gtk_widget_set_css_classes(GTK_WIDGET(label), NULL);
    gtk_label_set_text(label, text);
    gtk_label_set_attributes(label, attrs);
    gtk_widget_set_visible(GTK_WIDGET(label), TRUE);
    add_widget_to_self(self, GTK_WIDGET(label));

    hide_unused_widgets(self);
}

void
bob_launcher_match_row_label_set_description(BobLauncherMatchRowLabel *self,
                                              BobLauncherDescription *desc)
{
    g_return_if_fail(BOB_LAUNCHER_IS_MATCH_ROW_LABEL(self));

    reset(self);
    /* No longer unparenting - widgets stay parented, we just reuse them */

    /* If root is a container, apply its properties to self and process children */
    if (desc->fragment_type == BOB_LAUNCHER_FRAGMENT_TYPE_CONTAINER) {
        set_widget_css_class(GTK_WIDGET(self), desc->css_class);
        g_object_set_data(G_OBJECT(self), "fragment", NULL);

        if (desc->fragment_func != NULL) {
            g_object_set_data_full(G_OBJECT(self), "fragment", g_object_ref(desc), g_object_unref);
            gtk_widget_add_css_class(GTK_WIDGET(self), "clickable");
        }

        if (desc->children != NULL) {
            for (guint i = 0; i < desc->children->len; i++) {
                BobLauncherDescription *child_desc = g_ptr_array_index(desc->children, i);
                process_node(self, child_desc);
            }
        }
    } else {
        /* Root is a single node (unusual case) */
        process_node(self, desc);
    }

    hide_unused_widgets(self);
}

void
bob_launcher_match_row_label_reset(BobLauncherMatchRowLabel *self)
{
    g_return_if_fail(BOB_LAUNCHER_IS_MATCH_ROW_LABEL(self));

    reset(self);

    BobLauncherMatchRowLabelPrivate *priv = self->priv;
    for (guint i = 0; i < priv->labels->len; i++) {
        GtkLabel *label = g_ptr_array_index(priv->labels, i);
        gtk_label_set_attributes(label, NULL);
        gtk_label_set_text(label, "");
    }
}

BobLauncherMatchRowLabel *
bob_launcher_match_row_label_new(gchar **css_classes, gint css_classes_length1)
{
    BobLauncherMatchRowLabel *self = g_object_new(BOB_LAUNCHER_TYPE_MATCH_ROW_LABEL, NULL);
    if (css_classes != NULL) {
        gtk_widget_set_css_classes(GTK_WIDGET(self), (const char**)css_classes);
    }
    return self;
}

static void
bob_launcher_match_row_label_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
    BobLauncherMatchRowLabel *self = BOB_LAUNCHER_MATCH_ROW_LABEL(widget);
    BobLauncherMatchRowLabelPrivate *priv = self->priv;

    int width = gtk_widget_get_width(widget);
    gboolean need_left_mask = priv->scroll_position > 0;
    gboolean need_right_mask = priv->scroll_position < (priv->children_width - width);

    if (!need_left_mask && !need_right_mask) {
        GtkWidget *sibling = gtk_widget_get_first_child(widget);
        int count = 0;
        while (sibling != NULL && count++ < priv->current_widget_index) {
            gtk_widget_snapshot_child(widget, sibling, snapshot);
            sibling = gtk_widget_get_next_sibling(sibling);
        }
        return;
    }

    if (need_left_mask) {
        float progress = MIN((float)fabs(priv->scroll_position), FADE_WIDTH);
        float t = progress / FADE_WIDTH;
        float eased = FADE_WIDTH * (1 - (1 - t) * (1 - t) * (1 - t));
        priv->stops[0] = (GskColorStop){ 0.0f, { 0, 0, 0, 0 } };
        priv->stops[1] = (GskColorStop){ eased / width, { 0, 0, 0, 1 } };
    } else {
        priv->stops[0] = (GskColorStop){ 0.0f, { 0, 0, 0, 1 } };
        priv->stops[1] = (GskColorStop){ 0.0f, { 0, 0, 0, 1 } };
    }

    if (need_right_mask) {
        float progress = MIN((float)(priv->children_width - width - priv->scroll_position), FADE_WIDTH);
        float t = progress / FADE_WIDTH;
        float eased = FADE_WIDTH * (1 - (1 - t) * (1 - t) * (1 - t));
        priv->stops[2] = (GskColorStop){ 1.0f - (eased / width), { 0, 0, 0, 1 } };
        priv->stops[3] = (GskColorStop){ 1.0f, { 0, 0, 0, 0 } };
    } else {
        priv->stops[2] = (GskColorStop){ 1.0f, { 0, 0, 0, 1 } };
        priv->stops[3] = (GskColorStop){ 1.0f, { 0, 0, 0, 1 } };
    }

    if (!gtk_widget_compute_bounds(widget, widget, &priv->bounds)) return;
    gtk_snapshot_push_mask(snapshot, GSK_MASK_MODE_ALPHA);
    gtk_snapshot_append_linear_gradient(snapshot, &priv->bounds,
                                        &priv->start,
                                        &GRAPHENE_POINT_INIT(width, 0),
                                        priv->stops, 4);
    gtk_snapshot_pop(snapshot);

    GtkWidget *sibling = gtk_widget_get_first_child(widget);
    int count = 0;
    while (sibling != NULL && count++ < priv->current_widget_index) {
        gtk_widget_snapshot_child(widget, sibling, snapshot);
        sibling = gtk_widget_get_next_sibling(sibling);
    }

    gtk_snapshot_pop(snapshot);

    double momentum = priv->total_overhang * 0.02;
    priv->total_overhang -= momentum;
    if (fabs(priv->total_overhang) > 0.15) {
        update_scroll_position(self, momentum);
    } else {
        priv->total_overhang = 0.0;
    }
}

static void
bob_launcher_match_row_label_size_allocate(GtkWidget *widget, int width, int height, int baseline)
{
    BobLauncherMatchRowLabel *self = BOB_LAUNCHER_MATCH_ROW_LABEL(widget);
    BobLauncherMatchRowLabelPrivate *priv = self->priv;

    priv->scroll_position = CLAMP(priv->scroll_position, 0, priv->children_width - width);
    GskTransform *transform = gsk_transform_translate(NULL, &GRAPHENE_POINT_INIT(-(float)priv->scroll_position, 0));

    GtkWidget *sibling = gtk_widget_get_first_child(widget);
    int count = 0;
    while (sibling != NULL && count < priv->current_widget_index) {
        int widget_width = priv->widget_lengths[count];
        gtk_widget_allocate(sibling, widget_width, height, priv->max_baseline, gsk_transform_ref(transform));
        transform = gsk_transform_translate(transform, &GRAPHENE_POINT_INIT(widget_width, 0));
        sibling = gtk_widget_get_next_sibling(sibling);
        count++;
    }

    gsk_transform_unref(transform);
}

static void
bob_launcher_match_row_label_measure(GtkWidget *widget, GtkOrientation orientation,
                                      int for_size, int *minimum, int *natural,
                                      int *minimum_baseline, int *natural_baseline)
{
    BobLauncherMatchRowLabel *self = BOB_LAUNCHER_MATCH_ROW_LABEL(widget);
    BobLauncherMatchRowLabelPrivate *priv = self->priv;

    *natural = *minimum = 0;
    *natural_baseline = *minimum_baseline = -1;

    if (orientation == GTK_ORIENTATION_VERTICAL) {
        GtkWidget *sibling = gtk_widget_get_first_child(widget);
        int count = 0;
        while (sibling != NULL && count++ < priv->current_widget_index) {
            int child_height, child_nat_baseline;
            gtk_widget_measure(sibling, GTK_ORIENTATION_VERTICAL, -1,
                               NULL, &child_height, NULL, &child_nat_baseline);
            *natural = MAX(child_height, *natural);
            priv->max_baseline = *minimum_baseline = *natural_baseline = MAX(child_nat_baseline, *natural_baseline);
            sibling = gtk_widget_get_next_sibling(sibling);
            *minimum = priv->max_baseline;
        }
    } else {
        priv->children_width = 0;
        GtkWidget *sibling = gtk_widget_get_first_child(widget);
        int count = 0;
        while (sibling != NULL && count < priv->current_widget_index) {
            int child_width;
            gtk_widget_measure(sibling, GTK_ORIENTATION_HORIZONTAL, -1,
                               NULL, &child_width, NULL, NULL);
            priv->widget_lengths[count] = child_width;
            *natural += child_width;
            priv->children_width += child_width;
            sibling = gtk_widget_get_next_sibling(sibling);
            count++;
        }
    }
}

static GtkSizeRequestMode
bob_launcher_match_row_label_get_request_mode(GtkWidget *widget)
{
    return GTK_SIZE_REQUEST_CONSTANT_SIZE;
}
