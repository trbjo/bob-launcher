#include "query-container.h"
#include "state.h"
#include "drag-controller.h"
#include <gtk/gtk.h>
#include <pango/pango.h>
#include <graphene-gobject.h>
#include <math.h>

#define TYPE_TO_SEARCH "Type to search…"
#define SELECT_PLUGIN "Select a plugin…"

typedef struct _BobLauncherMatch BobLauncherMatch;
typedef struct _BobLauncherAppSettings BobLauncherAppSettings;
typedef struct _BobLauncherAppSettingsLayerShell BobLauncherAppSettingsLayerShell;

typedef enum {
    TEXT_REPR_FALLBACK_PLG = 0,
    TEXT_REPR_PLG = 1,
    TEXT_REPR_FALLBACK_SRC = 2,
    TEXT_REPR_SRC = 3,
    TEXT_REPR_FALLBACK_ACT = 4,
    TEXT_REPR_ACT = 5,
    TEXT_REPR_FALLBACK_TAR = 6,
    TEXT_REPR_TAR = 7,
    TEXT_REPR_EMPTY = 8,
    TEXT_REPR_COUNT = 9
} TextRepr;

/* External declarations */
extern BobLauncherAppSettings *bob_launcher_app_settings_get_default(void);
extern BobLauncherAppSettingsLayerShell *bob_launcher_app_settings_get_layershell(BobLauncherAppSettings *self);
extern gboolean bob_launcher_app_settings_layer_shell_get_enabled(BobLauncherAppSettingsLayerShell *self);

extern BobLauncherMatch *state_selected_plugin(void);
extern BobLauncherMatch *state_selected_source(void);
extern BobLauncherMatch *state_selected_action(void);

extern gchar *bob_launcher_match_get_title(BobLauncherMatch *self);
extern gchar *bob_launcher_match_get_icon_name(BobLauncherMatch *self);

extern GdkPaintable *icon_cache_service_get_paintable_for_icon_name(const char *name, int size, int scale);

typedef BobLauncherMatch *(*MatchFinderFunc)(double x, double y, gpointer user_data);
extern void bob_launcher_drag_and_drop_handler_setup(GtkWidget *widget, MatchFinderFunc func, gpointer user_data);

/* CursorWidget - internal helper class */
typedef struct _BobLauncherQueryContainerCursorWidget BobLauncherQueryContainerCursorWidget;
typedef struct _BobLauncherQueryContainerCursorWidgetClass BobLauncherQueryContainerCursorWidgetClass;

struct _BobLauncherQueryContainerCursorWidget {
    GtkWidget parent_instance;
};

struct _BobLauncherQueryContainerCursorWidgetClass {
    GtkWidgetClass parent_class;
};

static gpointer cursor_widget_parent_class = NULL;

static void
cursor_widget_class_init(BobLauncherQueryContainerCursorWidgetClass *klass, gpointer klass_data)
{
    (void)klass_data;
    cursor_widget_parent_class = g_type_class_peek_parent(klass);
}

static void
cursor_widget_instance_init(BobLauncherQueryContainerCursorWidget *self, gpointer klass)
{
    (void)klass;
    gtk_widget_set_name(GTK_WIDGET(self), "text-cursor");
}

static GType
cursor_widget_get_type(void)
{
    static gsize type_id = 0;
    if (g_once_init_enter(&type_id)) {
        static const GTypeInfo info = {
            sizeof(BobLauncherQueryContainerCursorWidgetClass),
            NULL, NULL,
            (GClassInitFunc)cursor_widget_class_init,
            NULL, NULL,
            sizeof(BobLauncherQueryContainerCursorWidget),
            0,
            (GInstanceInitFunc)cursor_widget_instance_init,
            NULL
        };
        GType id = g_type_register_static(GTK_TYPE_WIDGET, "BobLauncherQueryContainerCursorWidget", &info, 0);
        g_once_init_leave(&type_id, id);
    }
    return type_id;
}

static BobLauncherQueryContainerCursorWidget *
cursor_widget_new(void)
{
    return g_object_new(cursor_widget_get_type(), NULL);
}

/* QueryContainer */
struct _BobLauncherQueryContainer {
    GtkWidget parent_instance;
    int selected_row_height;
    BobLauncherQueryContainerCursorWidget *cursor_w;
    int cursor_position;
    float cursor_x;
    float cursor_y;
};

struct _BobLauncherQueryContainerClass {
    GtkWidgetClass parent_class;
};

static gpointer bob_launcher_query_container_parent_class = NULL;

static graphene_matrix_t color_matrix;
static graphene_vec4_t color_offset;
static PangoLayout *layouts[TEXT_REPR_COUNT];
static TextRepr text_repr = TEXT_REPR_EMPTY;
static BobLauncherQueryContainer *instance = NULL;

static void bob_launcher_query_container_class_init(BobLauncherQueryContainerClass *klass, gpointer klass_data);
static void bob_launcher_query_container_instance_init(BobLauncherQueryContainer *self, gpointer klass);
static void bob_launcher_query_container_finalize(GObject *obj);
static void bob_launcher_query_container_measure(GtkWidget *widget, GtkOrientation orientation, int for_size,
                                                  int *minimum, int *natural, int *minimum_baseline, int *natural_baseline);
static void bob_launcher_query_container_size_allocate(GtkWidget *widget, int width, int height, int baseline);
static void bob_launcher_query_container_snapshot(GtkWidget *widget, GtkSnapshot *snapshot);

static int
thumbnail_location(BobLauncherQueryContainer *self)
{
    return gtk_widget_get_width(GTK_WIDGET(self)) - gtk_widget_get_height(GTK_WIDGET(self));
}

static gboolean
should_drag(double x, double y, gpointer user_data)
{
    (void)y;
    (void)user_data;
    return !(x >= thumbnail_location(instance) && state_sf == bob_launcher_SEARCHING_FOR_ACTIONS);
}

static BobLauncherMatch *
match_finder(double x, double y, gpointer user_data)
{
    (void)y;
    (void)user_data;
    if (state_sf != bob_launcher_SEARCHING_FOR_ACTIONS) return NULL;
    if (x < thumbnail_location(instance)) return NULL;
    return state_selected_source();
}

static GType
bob_launcher_query_container_get_type_once(void)
{
    static const GTypeInfo info = {
        sizeof(BobLauncherQueryContainerClass),
        NULL, NULL,
        (GClassInitFunc)bob_launcher_query_container_class_init,
        NULL, NULL,
        sizeof(BobLauncherQueryContainer),
        0,
        (GInstanceInitFunc)bob_launcher_query_container_instance_init,
        NULL
    };
    return g_type_register_static(GTK_TYPE_WIDGET, "BobLauncherQueryContainer", &info, 0);
}

GType
bob_launcher_query_container_get_type(void)
{
    static volatile gsize type_id__once = 0;
    if (g_once_init_enter(&type_id__once)) {
        GType type_id = bob_launcher_query_container_get_type_once();
        g_once_init_leave(&type_id__once, type_id);
    }
    return type_id__once;
}

static void
bob_launcher_query_container_class_init(BobLauncherQueryContainerClass *klass, gpointer klass_data)
{
    (void)klass_data;

    bob_launcher_query_container_parent_class = g_type_class_peek_parent(klass);

    G_OBJECT_CLASS(klass)->finalize = bob_launcher_query_container_finalize;

    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
    widget_class->measure = bob_launcher_query_container_measure;
    widget_class->size_allocate = bob_launcher_query_container_size_allocate;
    widget_class->snapshot = bob_launcher_query_container_snapshot;

    /* Static construct - initialize color matrix */
    memset(&color_offset, 0, sizeof(graphene_vec4_t));
    memset(&color_matrix, 0, sizeof(graphene_matrix_t));

    float matrix_values[16] = {
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 1
    };
    graphene_matrix_init_from_float(&color_matrix, matrix_values);
}

static void
bob_launcher_query_container_instance_init(BobLauncherQueryContainer *self, gpointer klass)
{
    (void)klass;

    text_repr = TEXT_REPR_EMPTY;
    instance = self;
    self->selected_row_height = 0;
    self->cursor_position = 0;
    self->cursor_x = 0.0f;
    self->cursor_y = 0.0f;

    gtk_widget_set_name(GTK_WIDGET(self), "query-container");
    gtk_widget_set_overflow(GTK_WIDGET(self), GTK_OVERFLOW_HIDDEN);
    gtk_widget_add_css_class(GTK_WIDGET(self), "query-empty");

    for (int i = 0; i < TEXT_REPR_COUNT; i++) {
        layouts[i] = gtk_widget_create_pango_layout(GTK_WIDGET(self), NULL);
        pango_layout_set_ellipsize(layouts[i], PANGO_ELLIPSIZE_MIDDLE);
        pango_layout_set_single_paragraph_mode(layouts[i], TRUE);
    }
    pango_layout_set_text(layouts[TEXT_REPR_EMPTY], TYPE_TO_SEARCH, -1);
    pango_layout_set_text(layouts[TEXT_REPR_FALLBACK_PLG], SELECT_PLUGIN, -1);

    bob_launcher_drag_and_drop_handler_setup(GTK_WIDGET(self), match_finder, self);

    BobLauncherAppSettings *app_settings = bob_launcher_app_settings_get_default();
    BobLauncherAppSettingsLayerShell *layershell = bob_launcher_app_settings_get_layershell(app_settings);

    if (bob_launcher_app_settings_layer_shell_get_enabled(layershell)) {
        bob_launcher_setup_click_controller(GTK_WIDGET(self), layershell);
        bob_launcher_setup_drag_controller(self, layershell, should_drag, g_object_ref(self), g_object_unref);
    }

    self->cursor_w = cursor_widget_new();
    g_object_ref_sink(self->cursor_w);
    gtk_widget_set_parent(GTK_WIDGET(self->cursor_w), GTK_WIDGET(self));
}

static void
bob_launcher_query_container_finalize(GObject *obj)
{
    BobLauncherQueryContainer *self = BOB_LAUNCHER_QUERY_CONTAINER(obj);

    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(self))) != NULL) {
        gtk_widget_unparent(child);
    }

    if (self->cursor_w) {
        g_object_unref(self->cursor_w);
        self->cursor_w = NULL;
    }

    G_OBJECT_CLASS(bob_launcher_query_container_parent_class)->finalize(obj);
}

static void
bob_launcher_query_container_measure(GtkWidget *widget, GtkOrientation orientation, int for_size,
                                      int *minimum, int *natural, int *minimum_baseline, int *natural_baseline)
{
    (void)for_size;

    BobLauncherQueryContainer *self = BOB_LAUNCHER_QUERY_CONTAINER(widget);

    *minimum_baseline = *natural_baseline = -1;

    if (orientation == GTK_ORIENTATION_VERTICAL) {
        // int height;
        // pango_layout_get_size(layouts[TEXT_REPR_EMPTY], NULL, &height);
        pango_layout_get_pixel_size(layouts[TEXT_REPR_EMPTY], NULL, &self->selected_row_height);
        *minimum = *natural = self->selected_row_height;
    } else {
        *minimum = *natural = 0;
    }
}

static void
bob_launcher_query_container_size_allocate(GtkWidget *widget, int width, int height, int baseline)
{
    BobLauncherQueryContainer *self = BOB_LAUNCHER_QUERY_CONTAINER(widget);

    for (int i = TEXT_REPR_FALLBACK_PLG; i < TEXT_REPR_COUNT; i++) {
        int glass_width = (i == TEXT_REPR_FALLBACK_SRC) ? height : 0;
        int draggable_width = (i >= TEXT_REPR_FALLBACK_ACT) ? height : 0;
        int label_width = width - glass_width - draggable_width;
        int layout_width = label_width * PANGO_SCALE;
        pango_layout_set_width(layouts[i], layout_width);
        pango_layout_set_height(layouts[i], -1);
    }

    int cursor_width;
    gtk_widget_measure(GTK_WIDGET(self->cursor_w), GTK_ORIENTATION_HORIZONTAL, 0, NULL, &cursor_width, NULL, NULL);
    gtk_widget_allocate(GTK_WIDGET(self->cursor_w), cursor_width, self->selected_row_height, baseline, NULL);
}

static void
snapshot_glass(BobLauncherQueryContainer *self, GtkSnapshot *snapshot, GdkRGBA *color, int height)
{
    if (text_repr != TEXT_REPR_EMPTY) return;

    graphene_vec4_init(&color_offset, color->red, color->green, color->blue, 0);
    gtk_snapshot_push_color_matrix(snapshot, &color_matrix, &color_offset);

    GdkPaintable *glass = icon_cache_service_get_paintable_for_icon_name(
        "magnifying-glass-symbolic", height, gtk_widget_get_scale_factor(GTK_WIDGET(self)));
    gdk_paintable_snapshot(glass, snapshot, height, height);

    gtk_snapshot_pop(snapshot);

    graphene_point_t offset = GRAPHENE_POINT_INIT(height, 0);
    gtk_snapshot_translate(snapshot, &offset);
}

static void
snapshot_drag_image(BobLauncherQueryContainer *self, GtkSnapshot *snapshot, int height)
{
    if (state_sf < bob_launcher_SEARCHING_FOR_ACTIONS) return;

    BobLauncherMatch *m = state_selected_source();
    if (m == NULL) return;

    GdkPaintable *match_icon = icon_cache_service_get_paintable_for_icon_name(
        bob_launcher_match_get_icon_name(m), height, gtk_widget_get_scale_factor(GTK_WIDGET(self)));

    gtk_snapshot_save(snapshot);
    graphene_point_t offset = GRAPHENE_POINT_INIT(thumbnail_location(self), 0);
    gtk_snapshot_translate(snapshot, &offset);
    gdk_paintable_snapshot(match_icon, snapshot, height, height);
    gtk_snapshot_restore(snapshot);
}

static void
snapshot_cursor(BobLauncherQueryContainer *self, GtkSnapshot *snapshot)
{
    graphene_point_t offset = GRAPHENE_POINT_INIT(self->cursor_x, self->cursor_y);
    gtk_snapshot_translate(snapshot, &offset);
    gtk_widget_snapshot_child(GTK_WIDGET(self), GTK_WIDGET(self->cursor_w), snapshot);
}

static void
bob_launcher_query_container_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
    BobLauncherQueryContainer *self = BOB_LAUNCHER_QUERY_CONTAINER(widget);

    GdkRGBA color;
    gtk_widget_get_color(widget, &color);

    float font_alpha = color.alpha;
    color.alpha = 1.0f;
    int height = gtk_widget_get_height(widget);

    snapshot_drag_image(self, snapshot, height);

    gtk_snapshot_push_opacity(snapshot, font_alpha);

    snapshot_glass(self, snapshot, &color, height);

    graphene_point_t text_offset = GRAPHENE_POINT_INIT(0, (height - self->selected_row_height) / 2.0f);
    gtk_snapshot_translate(snapshot, &text_offset);
    gtk_snapshot_append_layout(snapshot, layouts[text_repr], &color);
    snapshot_cursor(self, snapshot);

    gtk_snapshot_pop(snapshot);

    gtk_widget_add_css_class(GTK_WIDGET(self->cursor_w), "blinking");
}

BobLauncherQueryContainer *
bob_launcher_query_container_new(void)
{
    return g_object_new(bob_launcher_query_container_get_type(), NULL);
}

void
bob_launcher_query_container_set_cursor_position(BobLauncherQueryContainer *self, int position)
{
    self->cursor_position = position;
    gtk_widget_remove_css_class(GTK_WIDGET(self->cursor_w), "blinking");
}

void
bob_launcher_query_container_adjust_label_for_query(const char *text, int cursor_position)
{
    int has_text = (text != NULL && text[0] != '\0') ? 1 : 0;
    text_repr = (TextRepr)(state_sf * 2 + has_text);

    bob_launcher_query_container_set_cursor_position(instance, cursor_position);

    switch (text_repr) {
    case TEXT_REPR_FALLBACK_PLG:
        break;

    case TEXT_REPR_FALLBACK_SRC: {
        BobLauncherMatch *sb = state_selected_plugin();
        if (sb != NULL) {
            pango_layout_set_text(layouts[text_repr], bob_launcher_match_get_title(sb), -1);
        } else {
            text_repr = TEXT_REPR_EMPTY;
        }
        break;
    }

    case TEXT_REPR_FALLBACK_ACT:
        pango_layout_set_text(layouts[text_repr], bob_launcher_match_get_title(state_selected_source()), -1);
        break;

    case TEXT_REPR_FALLBACK_TAR:
        pango_layout_set_text(layouts[text_repr], bob_launcher_match_get_title(state_selected_action()), -1);
        break;

    case TEXT_REPR_PLG:
    case TEXT_REPR_SRC:
    case TEXT_REPR_ACT:
    case TEXT_REPR_TAR:
        pango_layout_set_text(layouts[text_repr], text, -1);
        break;

    case TEXT_REPR_COUNT:
    case TEXT_REPR_EMPTY:
        break;
    }

    PangoRectangle strong_pos;
    pango_layout_get_cursor_pos(layouts[text_repr], instance->cursor_position, &strong_pos, NULL);
    instance->cursor_x = (float)strong_pos.x / PANGO_SCALE;
    instance->cursor_y = (float)strong_pos.y / PANGO_SCALE;

    if (text_repr % 2 == 0) {
        gtk_widget_add_css_class(GTK_WIDGET(instance), "query-empty");
    } else {
        gtk_widget_remove_css_class(GTK_WIDGET(instance), "query-empty");
    }

    gtk_widget_queue_draw(GTK_WIDGET(instance));
}
