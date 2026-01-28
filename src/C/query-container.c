#include "utf8-utils.h"
#include "query-container.h"
#include "state.h"
#include "drag-controller.h"
#include <gtk/gtk.h>
#include <gsk/gsk.h>
#include <pango/pango.h>
#include <graphene-gobject.h>
#include <math.h>

#define TYPE_TO_SEARCH "Type to search…"
#define SELECT_PLUGIN "Select a plugin…"

typedef struct _BobLauncherMatch BobLauncherMatch;
typedef struct _BobLauncherAppSettings BobLauncherAppSettings;
typedef struct _BobLauncherAppSettingsLayerShell BobLauncherAppSettingsLayerShell;

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

typedef struct _BobLauncherQueryContainerCursorWidget BobLauncherQueryContainerCursorWidget;
typedef struct _BobLauncherQueryContainerCursorWidgetClass BobLauncherQueryContainerCursorWidgetClass;

typedef enum {
    DISPLAY_MODE_PLUGIN_PLACEHOLDER = 0,
    DISPLAY_MODE_PLUGIN_TEXT = 1,
    DISPLAY_MODE_SOURCE_PLACEHOLDER = 2,
    DISPLAY_MODE_SOURCE_TEXT = 3,
    DISPLAY_MODE_ACTION_PLACEHOLDER = 4,
    DISPLAY_MODE_ACTION_TEXT = 5,
    DISPLAY_MODE_TARGET_PLACEHOLDER = 6,
    DISPLAY_MODE_TARGET_TEXT = 7,
    DISPLAY_MODE_EMPTY_WITH_ICON = 8,
} QueryDisplayMode;

typedef struct {
    QueryDisplayMode display_mode;
    GdkPaintable* match_icon;
    float cursor_x;
    float cursor_y;
} QueryRenderOutput;

typedef struct {
    int cursor_pos;
    QueryDisplayMode display_mode;
    char* text;
    char* icon_name;
} QueryRenderInput;

static void free_render_input(void* data) {
    QueryRenderInput* input = (QueryRenderInput*)data;
    free(input->text);
    free(input->icon_name);
    free(input);
}

struct _BobLauncherQueryContainer {
    GtkWidget parent_instance;
    int last_text_height;
    BobLauncherQueryContainerCursorWidget *cursor_widget;
    PangoLayout *base_layout;
    graphene_matrix_t color_matrix;
    graphene_vec4_t color_offset;
    GdkPaintable *search_icon;
    QueryRenderOutput* current_render_output;
    int last_width;
    int last_height;
    int scale_factor;
};


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

struct _BobLauncherQueryContainerClass {
    GtkWidgetClass parent_class;
};

static gpointer bob_launcher_query_container_parent_class = NULL;
static BobLauncherQueryContainer *instance = NULL;

static void bob_launcher_query_container_class_init(BobLauncherQueryContainerClass *klass, gpointer klass_data);
static void bob_launcher_query_container_instance_init(BobLauncherQueryContainer *self, gpointer klass);
static void bob_launcher_query_container_finalize(GObject *obj);
static void bob_launcher_query_container_measure(GtkWidget *widget, GtkOrientation orientation, int for_size,
                                                  int *minimum, int *natural, int *minimum_baseline, int *natural_baseline);
static void bob_launcher_query_container_size_allocate(GtkWidget *widget, int width, int height, int baseline);
static void bob_launcher_query_container_snapshot(GtkWidget *widget, GtkSnapshot *snapshot);

static QueryDisplayMode get_display_mode(const char* text) {
    bool has_query = (text != NULL && text[0] != '\0') ? 1 : 0;
    QueryDisplayMode mode = (QueryDisplayMode)(state_sf * 2 + has_query);
    if (!has_query && state_sf == bob_launcher_SEARCHING_FOR_SOURCES && state_selected_plugin() == NULL) {
        mode = DISPLAY_MODE_EMPTY_WITH_ICON;
    }
    return mode;
}

static const char* get_display_text(const char* text) {
    bool has_query = (text != NULL && text[0] != '\0') ? 1 : 0;

    if (has_query) {
        return text;
    }

    switch (state_sf) {
    case bob_launcher_SEARCHING_FOR_PLUGINS:
        return SELECT_PLUGIN;
    case bob_launcher_SEARCHING_FOR_ACTIONS:
        return bob_launcher_match_get_title(state_selected_source());
    case bob_launcher_SEARCHING_FOR_TARGETS:
        return bob_launcher_match_get_title(state_selected_action());
    case bob_launcher_SEARCHING_FOR_SOURCES:
    default:
        BobLauncherMatch *selected_plugin = state_selected_plugin();
        if (selected_plugin == NULL) {
            return TYPE_TO_SEARCH;
        } else {
            return bob_launcher_match_get_title(selected_plugin);
        }
    }
}

static void build_render_nodes(QueryRenderInput* input) {
    if (instance->last_width <= 0 || instance->last_height <= 0) return;

    int icon_width = (input->display_mode == DISPLAY_MODE_EMPTY_WITH_ICON) ? instance->last_height : 0;
    int draggable_width = (input->display_mode >= DISPLAY_MODE_ACTION_PLACEHOLDER) ? instance->last_height : 0;
    int layout_width = instance->last_width - icon_width - draggable_width;

    QueryRenderOutput *output = instance->current_render_output;

    size_t cursor_byte_pos = utf8_char_to_byte_pos(input->text, input->cursor_pos);
    PangoRectangle cursor_rect;

    pango_layout_set_width(instance->base_layout, layout_width * PANGO_SCALE);
    pango_layout_set_text(instance->base_layout, input->text, -1);
    pango_layout_get_cursor_pos(instance->base_layout, cursor_byte_pos, &cursor_rect, NULL);

    output->display_mode = input->display_mode;
    if (input->icon_name) {
        GdkPaintable *icon = icon_cache_service_get_paintable_for_icon_name(
            input->icon_name, instance->last_height, instance->scale_factor);
        if (icon) {
            output->match_icon = g_object_ref(icon);
        }
    } else {
        g_clear_object(&output->match_icon);
    }

    output->cursor_x = (float)cursor_rect.x / PANGO_SCALE;
    output->cursor_y = (float)cursor_rect.y / PANGO_SCALE;

    gtk_widget_remove_css_class(GTK_WIDGET(instance->cursor_widget), "blinking");
    if (input->display_mode % 2 == 0) {
        gtk_widget_add_css_class(GTK_WIDGET(instance), "query-empty");
    } else {
        gtk_widget_remove_css_class(GTK_WIDGET(instance), "query-empty");
    }

    gtk_widget_queue_draw(GTK_WIDGET(instance));
    free_render_input(input);
}

static const char* get_selected_source_icon_name() {
    if (state_sf < bob_launcher_SEARCHING_FOR_ACTIONS) return NULL;

    BobLauncherMatch *source_match = state_selected_source();
    if (source_match == NULL) return NULL;

    return bob_launcher_match_get_icon_name(source_match);
}

static QueryRenderInput* prepare_render_input() {
    QueryRenderInput* input = malloc(sizeof(QueryRenderInput));

    const char* text = state_get_query();

    input->text = strdup(get_display_text(text));
    const char* icon_name = get_selected_source_icon_name();

    input->icon_name = icon_name ? strdup(icon_name) : NULL;
    input->display_mode = get_display_mode(text);
    input->cursor_pos = state_get_cursor_position();
    return input;
}

static int
get_icon_x_position(BobLauncherQueryContainer *self)
{
    return self->last_width - self->last_text_height;
}

static gboolean
is_in_text_area(double x, double y, gpointer user_data)
{
    (void)y;
    (void)user_data;
    return !(x >= get_icon_x_position(instance) && state_sf == bob_launcher_SEARCHING_FOR_ACTIONS);
}

static BobLauncherMatch *
match_finder(double x, double y, gpointer user_data)
{
    (void)y;
    (void)user_data;
    if (state_sf != bob_launcher_SEARCHING_FOR_ACTIONS) return NULL;
    if (x < get_icon_x_position(instance)) return NULL;
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
}

static void
bob_launcher_query_container_instance_init(BobLauncherQueryContainer *self, gpointer klass)
{
    (void)klass;

    instance = self;
    self->last_text_height = 0;
    self->last_width = -1;
    self->last_height = -1;

    float matrix_values[16] = {
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 1
    };
    graphene_matrix_init_from_float(&self->color_matrix, matrix_values);

    gtk_widget_set_name(GTK_WIDGET(self), "query-container");
    gtk_widget_set_overflow(GTK_WIDGET(self), GTK_OVERFLOW_HIDDEN);

    self->base_layout = gtk_widget_create_pango_layout(GTK_WIDGET(self), NULL);
    pango_layout_set_ellipsize(self->base_layout, PANGO_ELLIPSIZE_MIDDLE);
    pango_layout_set_single_paragraph_mode(self->base_layout, TRUE);

    bob_launcher_drag_and_drop_handler_setup(GTK_WIDGET(self), match_finder, self);

    BobLauncherAppSettings *app_settings = bob_launcher_app_settings_get_default();
    BobLauncherAppSettingsLayerShell *layershell = bob_launcher_app_settings_get_layershell(app_settings);

    if (bob_launcher_app_settings_layer_shell_get_enabled(layershell)) {
        bob_launcher_setup_click_controller(GTK_WIDGET(self), layershell);
        bob_launcher_setup_drag_controller(self, layershell, is_in_text_area, g_object_ref(self), g_object_unref);
    }

    self->cursor_widget = cursor_widget_new();
    g_object_ref_sink(self->cursor_widget);
    gtk_widget_set_parent(GTK_WIDGET(self->cursor_widget), GTK_WIDGET(self));
    gtk_widget_add_css_class(GTK_WIDGET(instance->cursor_widget), "query-empty");
    gtk_widget_add_css_class(GTK_WIDGET(self->cursor_widget), "text-cursor");
    gtk_widget_add_css_class(GTK_WIDGET(self->cursor_widget), "text");

    self->scale_factor = gtk_widget_get_scale_factor(GTK_WIDGET(self));
    self->current_render_output = calloc(1, sizeof(QueryRenderOutput));
}

static void
bob_launcher_query_container_finalize(GObject *obj)
{
    BobLauncherQueryContainer *self = BOB_LAUNCHER_QUERY_CONTAINER(obj);

    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(self))) != NULL) {
        gtk_widget_unparent(child);
    }

    if (self->cursor_widget) {
        g_object_unref(self->cursor_widget);
        self->cursor_widget = NULL;
    }

    if (self->base_layout) {
        g_object_unref(self->base_layout);
        self->base_layout = NULL;
    }

    g_clear_object(&self->search_icon);

    g_clear_object(&self->current_render_output->match_icon);
    free(self->current_render_output);

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
        int text_height;

        pango_layout_get_pixel_size(self->base_layout, NULL, &text_height);

        *minimum = *natural = text_height;

        if (self->last_text_height != text_height) {
            self->last_text_height = text_height;
            g_clear_object(&self->search_icon);
            self->search_icon = g_object_ref(icon_cache_service_get_paintable_for_icon_name(
                "magnifying-glass-symbolic", text_height, gtk_widget_get_scale_factor(GTK_WIDGET(self))));
            build_render_nodes(prepare_render_input());
        }
    } else {
        *minimum = *natural = 0;
    }
}

static void
bob_launcher_query_container_size_allocate(GtkWidget *widget, int width, int height, int baseline)
{
    BobLauncherQueryContainer *self = BOB_LAUNCHER_QUERY_CONTAINER(widget);

    int cursor_width;
    gtk_widget_measure(GTK_WIDGET(self->cursor_widget), GTK_ORIENTATION_HORIZONTAL, 0, NULL, &cursor_width, NULL, NULL);
    gtk_widget_allocate(GTK_WIDGET(self->cursor_widget), cursor_width, self->last_text_height, baseline, NULL);

    if (self->last_height != height) {
        self->last_height = height;
    }

    if (self->last_width != width) {
        self->last_width = width;
        build_render_nodes(prepare_render_input());
    }
}

static inline void
snapshot_search_icon(BobLauncherQueryContainer *self, GtkSnapshot *snapshot, GdkRGBA *color, QueryRenderOutput* output)
{
    if (output->display_mode != DISPLAY_MODE_EMPTY_WITH_ICON) return;
    graphene_vec4_init(&self->color_offset, color->red, color->green, color->blue, 0);
    gtk_snapshot_push_color_matrix(snapshot, &self->color_matrix, &self->color_offset);
    gdk_paintable_snapshot(self->search_icon, snapshot, self->last_text_height, self->last_text_height);
    gtk_snapshot_pop(snapshot);

    graphene_point_t offset = GRAPHENE_POINT_INIT(self->last_text_height, 0);
    gtk_snapshot_translate(snapshot, &offset);
}

static void
snapshot_source_icon(BobLauncherQueryContainer *self, GtkSnapshot *snapshot, GdkPaintable *match_icon)
{
    if (!match_icon) return;
    graphene_point_t offset = GRAPHENE_POINT_INIT(get_icon_x_position(self), 0);
    gtk_snapshot_save(snapshot);
    gtk_snapshot_translate(snapshot, &offset);
    gdk_paintable_snapshot(match_icon, snapshot, self->last_text_height, self->last_text_height);
    gtk_snapshot_restore(snapshot);
}

static void
snapshot_cursor(BobLauncherQueryContainer *self, GtkSnapshot *snapshot, float cursor_x, float cursor_y)
{
    graphene_point_t offset = GRAPHENE_POINT_INIT(cursor_x, cursor_y);
    gtk_snapshot_save(snapshot);
    gtk_snapshot_translate(snapshot, &offset);
    gtk_widget_snapshot_child(GTK_WIDGET(self), GTK_WIDGET(self->cursor_widget), snapshot);
    gtk_snapshot_restore(snapshot);
}

static void
bob_launcher_query_container_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{

    BobLauncherQueryContainer *self = BOB_LAUNCHER_QUERY_CONTAINER(widget);

    QueryRenderOutput *output = self->current_render_output;

    int vertical_offset = self->last_height - self->last_text_height;
    if (vertical_offset) {
        graphene_point_t text_offset = GRAPHENE_POINT_INIT(0, (float)vertical_offset / 2.0f);
        gtk_snapshot_translate(snapshot, &text_offset);
    }

    GdkRGBA color;
    gtk_widget_get_color(GTK_WIDGET(instance), &color);
    gtk_snapshot_push_opacity(snapshot, color.alpha);

    snapshot_search_icon(self, snapshot, &color, output);

    snapshot_cursor(self, snapshot, output->cursor_x, output->cursor_y);

    if (output->display_mode % 2 == 0) {
        color.alpha = 1.0f; // don't apply twice.
    }

    gtk_snapshot_append_layout(snapshot, self->base_layout, &color);

    gtk_snapshot_pop(snapshot);

    snapshot_source_icon(self, snapshot, output->match_icon);

    gtk_widget_add_css_class(GTK_WIDGET(instance->cursor_widget), "blinking");
}

BobLauncherQueryContainer *
bob_launcher_query_container_new(void)
{
    return g_object_new(bob_launcher_query_container_get_type(), NULL);
}

void bob_launcher_query_container_adjust_label_for_query() {
    build_render_nodes(prepare_render_input());
}
