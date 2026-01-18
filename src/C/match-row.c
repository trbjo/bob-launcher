#include "match-row.h"
#include "bob-launcher.h"
#include "description.h"
#include <match-row-label.h>
#include <highlight.h>
#include <state.h>
#include <hashset.h>
#include <match.h>
#include <icon-cache-service.h>
#include <gdk/gdk.h>
#include <pango/pango.h>
#include <gsk/gsk.h>
#include <stdatomic.h>

/* ============================================================================
 * Constants
 * ============================================================================ */

#define SHORTCUT_CSS    "shortcut"
#define HORIZONTAL_CSS  "horizontal"
#define VERTICAL_CSS    "vertical"

/* ============================================================================
 * External type declarations
 * ============================================================================ */

typedef struct _BobLauncherAppSettings BobLauncherAppSettings;

/* We need field access to BobLauncherAppSettingsUI for the 'settings' field */
typedef struct _BobLauncherAppSettingsUIPrivate BobLauncherAppSettingsUIPrivate;
typedef struct _BobLauncherAppSettingsUI BobLauncherAppSettingsUI;
typedef struct _BobLauncherAppSettingsUIClass BobLauncherAppSettingsUIClass;

struct _BobLauncherAppSettingsUI {
    GTypeInstance parent_instance;
    volatile int ref_count;
    BobLauncherAppSettingsUIPrivate *priv;
    GSettings *settings;
};

struct _BobLauncherAppSettingsUIClass {
    GTypeClass parent_class;
    void (*finalize)(BobLauncherAppSettingsUI *self);
};

/* ============================================================================
 * External function declarations
 * ============================================================================ */

extern GType bob_launcher_row_number_get_type(void);
extern BobLauncherAppSettings *bob_launcher_app_settings_get_default(void);
extern BobLauncherAppSettingsUI *bob_launcher_app_settings_get_ui(BobLauncherAppSettings *self);

/* RowNumber functions */
extern GtkWidget *bob_launcher_row_number_new(gint row_num);
extern void bob_launcher_row_number_update_row_num(GtkWidget *self, gint new_row);

/* MatchRowLabel functions are declared in <match-row-label.h> */

/* DragAndDropHandler */
typedef BobLauncherMatch *(*BobLauncherMatchFinderFunc)(gdouble x, gdouble y, gpointer user_data);
extern void bob_launcher_drag_and_drop_handler_setup(GtkWidget *widget,
                                                     BobLauncherMatchFinderFunc finder,
                                                     gpointer user_data);

/* Utils */
typedef void (*BobLauncherChildIterator)(GtkWidget *widget, gpointer user_data);
extern void bob_launcher_utils_iterate_children(GtkWidget *child, BobLauncherChildIterator func, gpointer data);

/* Type macros for RowNumber (not in any included header) */
#define BOB_LAUNCHER_TYPE_ROW_NUMBER (bob_launcher_row_number_get_type())
#define BOB_LAUNCHER_ROW_NUMBER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), BOB_LAUNCHER_TYPE_ROW_NUMBER, GtkWidget))

/* BOB_LAUNCHER_TYPE_MATCH_ROW_LABEL and BOB_LAUNCHER_MATCH_ROW_LABEL are in <match-row-label.h> */

/* ============================================================================
 * TooltipWrapper - Internal helper class (file-static)
 * ============================================================================ */

typedef struct _TooltipWrapper TooltipWrapper;
typedef struct _TooltipWrapperClass TooltipWrapperClass;
typedef struct _TooltipWrapperPrivate TooltipWrapperPrivate;

struct _TooltipWrapper {
    GtkWidget parent_instance;
    TooltipWrapperPrivate *priv;
};

struct _TooltipWrapperClass {
    GtkWidgetClass parent_class;
};

struct _TooltipWrapperPrivate {
    gint max_width;
    gint max_height;
    GtkWidget *child;
    GtkSizeRequestMode request_mode;
    GSettings *settings;
};

static gpointer tooltip_wrapper_parent_class = NULL;
static gint TooltipWrapper_private_offset;

static inline TooltipWrapperPrivate *
tooltip_wrapper_get_instance_private(TooltipWrapper *self)
{
    return G_STRUCT_MEMBER_P(self, TooltipWrapper_private_offset);
}

static GtkSizeRequestMode
tooltip_wrapper_get_request_mode(GtkWidget *widget)
{
    TooltipWrapper *self = (TooltipWrapper *)widget;
    return self->priv->request_mode;
}

static void
tooltip_wrapper_measure(GtkWidget *widget,
                        GtkOrientation orientation,
                        gint for_size,
                        gint *minimum,
                        gint *natural,
                        gint *minimum_baseline,
                        gint *natural_baseline)
{
    TooltipWrapper *self = (TooltipWrapper *)widget;
    TooltipWrapperPrivate *priv = self->priv;

    *minimum = *natural = *minimum_baseline = *natural_baseline = -1;

    if (priv->child == NULL)
        return;

    gint dim = (orientation == GTK_ORIENTATION_VERTICAL) ? priv->max_height : priv->max_width;
    if (for_size > -1)
        dim = MIN(for_size, dim);

    gtk_widget_measure(priv->child, orientation, dim, minimum, natural, minimum_baseline, natural_baseline);

    gint max_dim = (orientation == GTK_ORIENTATION_VERTICAL) ? priv->max_height : priv->max_width;
    *minimum = MIN(max_dim, *minimum);
    *natural = MIN(max_dim, *natural);
}

static void
tooltip_wrapper_size_allocate(GtkWidget *widget, gint width, gint height, gint baseline)
{
    TooltipWrapper *self = (TooltipWrapper *)widget;
    if (self->priv->child != NULL)
        gtk_widget_allocate(self->priv->child, width, height, baseline, NULL);
}

static void
tooltip_wrapper_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
    TooltipWrapper *self = (TooltipWrapper *)widget;
    if (self->priv->child != NULL)
        gtk_widget_snapshot_child(widget, self->priv->child, snapshot);
}

static void
tooltip_wrapper_change_widget(TooltipWrapper *self, GtkWidget *new_widget)
{
    TooltipWrapperPrivate *priv = self->priv;

    if (priv->child != NULL)
        gtk_widget_unparent(priv->child);

    gtk_widget_set_parent(new_widget, GTK_WIDGET(self));
    priv->child = new_widget;

    gint child_width, child_height;
    gtk_widget_measure(priv->child, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &child_width, NULL, NULL);
    gtk_widget_measure(priv->child, GTK_ORIENTATION_VERTICAL, -1, NULL, &child_height, NULL, NULL);

    priv->request_mode = (child_height > child_width)
        ? GTK_SIZE_REQUEST_WIDTH_FOR_HEIGHT
        : GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}

static void
tooltip_wrapper_finalize(GObject *obj)
{
    TooltipWrapper *self = (TooltipWrapper *)obj;
    g_clear_object(&self->priv->settings);
    G_OBJECT_CLASS(tooltip_wrapper_parent_class)->finalize(obj);
}

static void
tooltip_wrapper_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    TooltipWrapper *self = (TooltipWrapper *)object;
    switch (prop_id) {
        case 1: g_value_set_int(value, self->priv->max_width); break;
        case 2: g_value_set_int(value, self->priv->max_height); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec); break;
    }
}

static void
tooltip_wrapper_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    TooltipWrapper *self = (TooltipWrapper *)object;
    switch (prop_id) {
        case 1: self->priv->max_width = g_value_get_int(value); break;
        case 2: self->priv->max_height = g_value_get_int(value); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec); break;
    }
}

static void
tooltip_wrapper_class_init(TooltipWrapperClass *klass, gpointer klass_data)
{
    tooltip_wrapper_parent_class = g_type_class_peek_parent(klass);
    g_type_class_adjust_private_offset(klass, &TooltipWrapper_private_offset);

    GObjectClass *obj_class = G_OBJECT_CLASS(klass);
    obj_class->finalize = tooltip_wrapper_finalize;
    obj_class->get_property = tooltip_wrapper_get_property;
    obj_class->set_property = tooltip_wrapper_set_property;

    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
    widget_class->get_request_mode = tooltip_wrapper_get_request_mode;
    widget_class->measure = tooltip_wrapper_measure;
    widget_class->size_allocate = tooltip_wrapper_size_allocate;
    widget_class->snapshot = tooltip_wrapper_snapshot;

    gtk_widget_class_set_css_name(widget_class, "box");

    g_object_class_install_property(obj_class, 1,
        g_param_spec_int("max-width", NULL, NULL, G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));
    g_object_class_install_property(obj_class, 2,
        g_param_spec_int("max-height", NULL, NULL, G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));
}

static void
tooltip_wrapper_instance_init(TooltipWrapper *self, gpointer klass)
{
    self->priv = tooltip_wrapper_get_instance_private(self);
    self->priv->request_mode = GTK_SIZE_REQUEST_CONSTANT_SIZE;
    self->priv->child = NULL;

    self->priv->settings = g_settings_new(BOB_LAUNCHER_BOB_LAUNCHER_APP_ID ".ui");
    g_settings_bind(self->priv->settings, "tooltip-max-height", self, "max_height", G_SETTINGS_BIND_GET);
    g_settings_bind(self->priv->settings, "tooltip-max-width", self, "max_width", G_SETTINGS_BIND_GET);
}

static GType
tooltip_wrapper_get_type(void)
{
    static gsize type_id = 0;
    if (g_once_init_enter(&type_id)) {
        static const GTypeInfo info = {
            sizeof(TooltipWrapperClass),
            NULL, NULL,
            (GClassInitFunc)tooltip_wrapper_class_init,
            NULL, NULL,
            sizeof(TooltipWrapper),
            0,
            (GInstanceInitFunc)tooltip_wrapper_instance_init,
            NULL
        };
        GType id = g_type_register_static(GTK_TYPE_WIDGET, "BobLauncherMatchRowTooltipWrapper", &info, 0);
        TooltipWrapper_private_offset = g_type_add_instance_private(id, sizeof(TooltipWrapperPrivate));
        g_once_init_leave(&type_id, id);
    }
    return type_id;
}

static TooltipWrapper *
tooltip_wrapper_new(void)
{
    return g_object_new(tooltip_wrapper_get_type(), NULL);
}

/* ============================================================================
 * MatchRow Private Data
 * ============================================================================ */

struct _BobLauncherMatchRowPrivate {
    BobLauncherMatchRowLabel *description;
    BobLauncherMatchRowLabel *title;
    GtkWidget *selected_row;
    GtkLabel *shortcut;
    GtkWidget *icon_widget;

    gboolean was_interesting;
    GtkOrientation orientation;
    gint icon_size;
    gint match_row_height;
    gint selected_row_width;
    gint shortcut_width;

    gchar *title_string;
    gchar *description_string;
    gchar *icon_name;
    HighlightPositions *title_positions;
    HighlightPositions *description_positions;
    HighlightStyle highlight_style;

    Description *rich_description;

    gint title_width;
    gint desc_width;
    gint title_height;
    gint desc_height;
    gint title_nat_baseline;
    gint desc_nat_baseline;
};

/* ============================================================================
 * Static class variables
 * ============================================================================ */

static gpointer bob_launcher_match_row_parent_class = NULL;
static gint BobLauncherMatchRow_private_offset;

static GdkCursor *match_row_pointer = NULL;
static GSettings *match_row_settings = NULL;
static BobLauncherAppSettingsUI *match_row_ui_settings = NULL;
static TooltipWrapper *match_row_tooltip_wrapper = NULL;

/* ============================================================================
 * Private function declarations
 * ============================================================================ */

static inline BobLauncherMatchRowPrivate *
bob_launcher_match_row_get_instance_private(BobLauncherMatchRow *self)
{
    return G_STRUCT_MEMBER_P(self, BobLauncherMatchRow_private_offset);
}

static HighlightStyle parse_highlight_style(const gchar *styles);
static void update_ui(BobLauncherMatchRow *self);
static void update_styling(BobLauncherMatchRow *self);
static void on_highlight_style_changed(BobLauncherMatchRow *self);
static gboolean on_query_tooltip(BobLauncherMatchRow *self, gint x, gint y,
                                  gboolean keyboard_tooltip, GtkTooltip *tooltip);
static BobLauncherMatch *match_finder(gdouble x, gdouble y, gpointer user_data);

/* ============================================================================
 * Signal callbacks (static wrappers)
 * ============================================================================ */

static void
on_settings_changed_update_ui(GSettings *settings, const gchar *key, gpointer user_data)
{
    update_ui(BOB_LAUNCHER_MATCH_ROW(user_data));
}

static void
on_settings_changed_highlight_style(GSettings *settings, const gchar *key, gpointer user_data)
{
    on_highlight_style_changed(BOB_LAUNCHER_MATCH_ROW(user_data));
}

static void
on_accent_color_changed(gpointer sender, gpointer user_data)
{
    update_styling(BOB_LAUNCHER_MATCH_ROW(user_data));
}

static gboolean
on_query_tooltip_cb(GtkWidget *widget, gint x, gint y, gboolean keyboard, GtkTooltip *tooltip, gpointer user_data)
{
    return on_query_tooltip(BOB_LAUNCHER_MATCH_ROW(user_data), x, y, keyboard, tooltip);
}

/* ============================================================================
 * Private function implementations
 * ============================================================================ */

static HighlightStyle
parse_highlight_style(const gchar *styles)
{
    gchar **parts = g_strsplit(styles, "|", -1);
    HighlightStyle style = 0;

    for (gchar **p = parts; *p != NULL; p++) {
        if (g_strcmp0(*p, "underline") == 0)
            style |= HIGHLIGHT_STYLE_UNDERLINE;
        else if (g_strcmp0(*p, "bold") == 0)
            style |= HIGHLIGHT_STYLE_BOLD;
        else if (g_strcmp0(*p, "background") == 0)
            style |= HIGHLIGHT_STYLE_BACKGROUND;
        else if (g_strcmp0(*p, "color") == 0)
            style |= HIGHLIGHT_STYLE_COLOR;
        else
            g_warning("unrecognized color style: %s", *p);
    }

    g_strfreev(parts);
    return style;
}

static void
on_highlight_style_changed(BobLauncherMatchRow *self)
{
    gchar *style_string = g_settings_get_string(match_row_settings, "highlight-style");
    self->priv->highlight_style = parse_highlight_style(style_string);
    g_free(style_string);
    update_styling(self);
}

static void
update_ui(BobLauncherMatchRow *self)
{
    BobLauncherMatchRowPrivate *priv = self->priv;

    priv->match_row_height = 0;

    if (g_settings_get_boolean(match_row_settings, "match-description-next-to-title")) {
        priv->orientation = GTK_ORIENTATION_HORIZONTAL;
        gtk_widget_add_css_class(GTK_WIDGET(self), HORIZONTAL_CSS);
        gtk_widget_remove_css_class(GTK_WIDGET(self), VERTICAL_CSS);
    } else {
        priv->orientation = GTK_ORIENTATION_VERTICAL;
        gtk_widget_remove_css_class(GTK_WIDGET(self), HORIZONTAL_CSS);
        gtk_widget_add_css_class(GTK_WIDGET(self), VERTICAL_CSS);
    }

    priv->icon_size = g_settings_get_int(match_row_settings, "match-icon-size");

    gchar *shortcut_markup = g_settings_get_string(match_row_settings, "shortcut-indicator");
    gtk_label_set_markup(priv->shortcut, shortcut_markup);
    g_free(shortcut_markup);

    gtk_widget_queue_resize(GTK_WIDGET(self));
}

static void
update_styling(BobLauncherMatchRow *self)
{
    BobLauncherMatchRowPrivate *priv = self->priv;
    const GdkRGBA *accent_color = highlight_get_accent_color();

    if (priv->title_string != NULL && priv->title_positions != NULL) {
        PangoAttrList *attrs = highlight_apply_style(priv->title_positions, priv->highlight_style, accent_color);
        bob_launcher_match_row_label_set_text(priv->title, priv->title_string, attrs);
        pango_attr_list_unref(attrs);
    }

    if (priv->description_string != NULL && priv->description_positions != NULL) {
        PangoAttrList *attrs = highlight_apply_style(priv->description_positions, priv->highlight_style, accent_color);
        bob_launcher_match_row_label_set_text(priv->description, priv->description_string, attrs);
        pango_attr_list_unref(attrs);
    }

    gtk_widget_queue_draw(GTK_WIDGET(self));
}

static gboolean
on_query_tooltip(BobLauncherMatchRow *self, gint x, gint y, gboolean keyboard_tooltip, GtkTooltip *tooltip)
{
    BobLauncherMatchRowPrivate *priv = self->priv;

    GtkWidget *picked = gtk_widget_pick(GTK_WIDGET(self), (gdouble)x, (gdouble)y, GTK_PICK_DEFAULT);
    if (picked == NULL)
        return FALSE;

    gboolean is_interesting = gtk_widget_has_css_class(picked, "clickable");
    if (is_interesting != priv->was_interesting) {
        gtk_widget_set_cursor(GTK_WIDGET(self), is_interesting ? match_row_pointer : NULL);
        priv->was_interesting = is_interesting;
    }

    BobLauncherMatch *m = hashset_get_match_at(state_current_provider(), self->abs_index);
    if (m == NULL)
        return FALSE;

    GtkWidget *tooltip_w = bob_launcher_match_get_tooltip(m);
    if (tooltip_w == NULL)
        return FALSE;

    tooltip_wrapper_change_widget(match_row_tooltip_wrapper, tooltip_w);
    gtk_tooltip_set_custom(tooltip, GTK_WIDGET(match_row_tooltip_wrapper));
    return TRUE;
}

static BobLauncherMatch * match_finder(gdouble x, gdouble y, gpointer user_data) {
    BobLauncherMatchRow *self = BOB_LAUNCHER_MATCH_ROW(user_data);

    if (state_sf != bob_launcher_SEARCHING_FOR_SOURCES)
        return NULL;

    return hashset_get_match_at(state_providers[bob_launcher_SEARCHING_FOR_SOURCES], self->abs_index);
}

/* ============================================================================
 * GtkWidget virtual function implementations
 * ============================================================================ */

static GtkSizeRequestMode
bob_launcher_match_row_get_request_mode(GtkWidget *widget)
{
    return GTK_SIZE_REQUEST_CONSTANT_SIZE;
}

static void
bob_launcher_match_row_measure(GtkWidget *widget,
                               GtkOrientation o,
                               gint for_size,
                               gint *minimum,
                               gint *natural,
                               gint *minimum_baseline,
                               gint *natural_baseline)
{
    BobLauncherMatchRow *self = BOB_LAUNCHER_MATCH_ROW(widget);
    BobLauncherMatchRowPrivate *priv = self->priv;

    if (o == GTK_ORIENTATION_VERTICAL) {
        if (priv->match_row_height == 0) {
            gint shortcut_nat, selected_row_nat;

            gtk_widget_measure(GTK_WIDGET(priv->title), GTK_ORIENTATION_VERTICAL, -1,
                               NULL, &priv->title_height, NULL, &priv->title_nat_baseline);
            gtk_widget_measure(GTK_WIDGET(priv->description), GTK_ORIENTATION_VERTICAL, -1,
                               NULL, &priv->desc_height, NULL, &priv->desc_nat_baseline);
            gtk_widget_measure(GTK_WIDGET(priv->shortcut), GTK_ORIENTATION_VERTICAL, -1,
                               NULL, &shortcut_nat, NULL, NULL);
            gtk_widget_measure(priv->selected_row, GTK_ORIENTATION_VERTICAL, -1,
                               NULL, &selected_row_nat, NULL, NULL);

            if (priv->orientation == GTK_ORIENTATION_VERTICAL) {
                priv->match_row_height = MAX(MAX(MAX(priv->icon_size,
                    priv->title_height + priv->desc_height), shortcut_nat), selected_row_nat);
            } else {
                priv->match_row_height = MAX(MAX(MAX(MAX(priv->icon_size,
                    priv->title_height), shortcut_nat), selected_row_nat), priv->desc_height);
            }
        }

        *natural_baseline = MAX(priv->desc_nat_baseline, priv->title_nat_baseline);
        *minimum_baseline = *natural_baseline;
        *natural = priv->match_row_height;
        *minimum = *natural;
    } else {
        *minimum = *natural = 0;
        *minimum_baseline = *natural_baseline = -1;

        gtk_widget_measure(priv->selected_row, GTK_ORIENTATION_HORIZONTAL, -1,
                           NULL, &priv->selected_row_width, NULL, NULL);
        gtk_widget_measure(GTK_WIDGET(priv->shortcut), GTK_ORIENTATION_HORIZONTAL, -1,
                           NULL, &priv->shortcut_width, NULL, NULL);

        if (priv->orientation == GTK_ORIENTATION_HORIZONTAL) {
            gtk_widget_measure(GTK_WIDGET(priv->description), GTK_ORIENTATION_HORIZONTAL, -1,
                               NULL, &priv->desc_width, NULL, NULL);
            gtk_widget_measure(GTK_WIDGET(priv->title), GTK_ORIENTATION_HORIZONTAL, -1,
                               NULL, &priv->title_width, NULL, NULL);
        }
    }
}

static void
bob_launcher_match_row_size_allocate(GtkWidget *widget, gint width, gint height, gint baseline)
{
    BobLauncherMatchRow *self = BOB_LAUNCHER_MATCH_ROW(widget);
    BobLauncherMatchRowPrivate *priv = self->priv;

    const gfloat fheight = (gfloat)height;
    const gfloat fhalf = fheight * 0.5f;

    /* Allocate selected_row at right edge */
    graphene_point_t pt = GRAPHENE_POINT_INIT(width - priv->selected_row_width, 0);
    gtk_widget_allocate(priv->selected_row, priv->selected_row_width, height, baseline,
                        gsk_transform_translate(NULL, &pt));

    /* Allocate shortcut next to selected_row */
    pt.x = width - priv->shortcut_width - priv->selected_row_width;
    gtk_widget_allocate(GTK_WIDGET(priv->shortcut), priv->shortcut_width, height, baseline,
                        gsk_transform_translate(NULL, &pt));

    if (priv->orientation == GTK_ORIENTATION_VERTICAL) {
        const gint text_width = width - priv->icon_size - priv->shortcut_width - priv->selected_row_width;
        const gint leftover_height = height - priv->title_height - priv->desc_height;
        const gfloat middle_adj = leftover_height * 0.25f;

        pt.x = priv->icon_size;
        pt.y = middle_adj;
        gtk_widget_allocate(GTK_WIDGET(priv->title), text_width, priv->title_height, priv->title_nat_baseline,
                            gsk_transform_translate(NULL, &pt));

        pt.y = priv->title_height + middle_adj * 2.0f;
        gtk_widget_allocate(GTK_WIDGET(priv->description), text_width, priv->desc_height, priv->desc_nat_baseline,
                            gsk_transform_translate(NULL, &pt));
    } else {
        const gfloat title_label_shift = fhalf - (gfloat)priv->title_height * 0.5f;
        const gfloat desc_label_shift = (gfloat)(height - priv->desc_height) * 0.5f;
        const gint available = width - priv->icon_size - priv->shortcut_width - priv->selected_row_width;
        const gint title_w = MIN(available, priv->title_width);
        const gint desc_w = MIN(available, priv->desc_width);

        pt.x = priv->icon_size;
        pt.y = title_label_shift;
        gtk_widget_allocate(GTK_WIDGET(priv->title), title_w, priv->title_height, priv->title_nat_baseline,
                            gsk_transform_translate(NULL, &pt));

        pt.x = priv->icon_size + title_w;
        pt.y = desc_label_shift;
        gtk_widget_allocate(GTK_WIDGET(priv->description), desc_w, priv->desc_height, priv->desc_nat_baseline,
                            gsk_transform_translate(NULL, &pt));
    }

    if (priv->icon_widget != NULL) {
        pt.x = 0;
        pt.y = (gfloat)(gtk_widget_get_height(widget) - priv->icon_size) * 0.5f;
        gtk_widget_allocate(priv->icon_widget, priv->icon_size, priv->icon_size, -1,
                            gsk_transform_translate(NULL, &pt));
    }
}

static void
bob_launcher_match_row_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
    BobLauncherMatchRow *self = BOB_LAUNCHER_MATCH_ROW(widget);
    BobLauncherMatchRowPrivate *priv = self->priv;

    gtk_widget_snapshot_child(widget, GTK_WIDGET(priv->title), snapshot);
    gtk_widget_snapshot_child(widget, GTK_WIDGET(priv->description), snapshot);
    gtk_widget_snapshot_child(widget, priv->selected_row, snapshot);
    gtk_widget_snapshot_child(widget, GTK_WIDGET(priv->shortcut), snapshot);

    if (priv->icon_widget != NULL) {
        gtk_widget_snapshot_child(widget, priv->icon_widget, snapshot);
    } else {
        graphene_point_t pt = GRAPHENE_POINT_INIT(0, (gfloat)(gtk_widget_get_height(widget) - priv->icon_size) * 0.5f);
        gtk_snapshot_translate(snapshot, &pt);

        GdkPaintable *paintable = icon_cache_service_get_paintable_for_icon_name(
            priv->icon_name, priv->icon_size, gtk_widget_get_scale_factor(widget));
        gdk_paintable_snapshot(paintable, GDK_SNAPSHOT(snapshot), priv->icon_size, priv->icon_size);
    }
}

/* ============================================================================
 * GObject lifecycle
 * ============================================================================ */

static void
unparent_child(GtkWidget *child, gpointer user_data)
{
    gtk_widget_unparent(child);
}

static void
bob_launcher_match_row_dispose(GObject *obj)
{
    BobLauncherMatchRow *self = BOB_LAUNCHER_MATCH_ROW(obj);
    BobLauncherMatchRowPrivate *priv = self->priv;

    g_clear_pointer(&priv->title_positions, highlight_positions_free);
    g_clear_pointer(&priv->description_positions, highlight_positions_free);

    bob_launcher_utils_iterate_children(gtk_widget_get_first_child(GTK_WIDGET(self)),
                                         unparent_child, NULL);

    G_OBJECT_CLASS(bob_launcher_match_row_parent_class)->dispose(obj);
}

static void
bob_launcher_match_row_finalize(GObject *obj)
{
    BobLauncherMatchRow *self = BOB_LAUNCHER_MATCH_ROW(obj);
    BobLauncherMatchRowPrivate *priv = self->priv;

    g_free(priv->title_string);
    g_free(priv->description_string);
    g_free(priv->icon_name);

    G_OBJECT_CLASS(bob_launcher_match_row_parent_class)->finalize(obj);
}

static void
bob_launcher_match_row_class_init(BobLauncherMatchRowClass *klass, gpointer klass_data)
{
    bob_launcher_match_row_parent_class = g_type_class_peek_parent(klass);
    g_type_class_adjust_private_offset(klass, &BobLauncherMatchRow_private_offset);

    GObjectClass *obj_class = G_OBJECT_CLASS(klass);
    obj_class->dispose = bob_launcher_match_row_dispose;
    obj_class->finalize = bob_launcher_match_row_finalize;

    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
    widget_class->get_request_mode = bob_launcher_match_row_get_request_mode;
    widget_class->measure = bob_launcher_match_row_measure;
    widget_class->size_allocate = bob_launcher_match_row_size_allocate;
    widget_class->snapshot = bob_launcher_match_row_snapshot;

    gtk_widget_class_set_css_name(widget_class, "match-row");

    /* Initialize static class variables */
    BobLauncherAppSettings *app_settings = bob_launcher_app_settings_get_default();
    match_row_ui_settings = bob_launcher_app_settings_get_ui(app_settings);
    match_row_settings = match_row_ui_settings->settings;
    match_row_pointer = gdk_cursor_new_from_name("pointer", NULL);
    match_row_tooltip_wrapper = tooltip_wrapper_new();
    g_object_ref_sink(match_row_tooltip_wrapper);
}

static void
bob_launcher_match_row_instance_init(BobLauncherMatchRow *self, gpointer klass)
{
    self->priv = bob_launcher_match_row_get_instance_private(self);
    BobLauncherMatchRowPrivate *priv = self->priv;

    priv->title_positions = NULL;
    priv->description_positions = NULL;
    priv->highlight_style = HIGHLIGHT_STYLE_COLOR;
    priv->title_height = 0;
    priv->desc_height = 0;
    priv->title_nat_baseline = 0;
    priv->desc_nat_baseline = 0;
}

/* ============================================================================
 * Type registration
 * ============================================================================ */

static GType
bob_launcher_match_row_get_type_once(void)
{
    static const GTypeInfo info = {
        sizeof(BobLauncherMatchRowClass),
        NULL, NULL,
        (GClassInitFunc)bob_launcher_match_row_class_init,
        NULL, NULL,
        sizeof(BobLauncherMatchRow),
        0,
        (GInstanceInitFunc)bob_launcher_match_row_instance_init,
        NULL
    };

    GType type_id = g_type_register_static(GTK_TYPE_WIDGET, "BobLauncherMatchRow", &info, 0);
    BobLauncherMatchRow_private_offset = g_type_add_instance_private(type_id, sizeof(BobLauncherMatchRowPrivate));
    return type_id;
}

GType
bob_launcher_match_row_get_type(void)
{
    static volatile gsize type_id_once = 0;
    if (g_once_init_enter(&type_id_once)) {
        GType type_id = bob_launcher_match_row_get_type_once();
        g_once_init_leave(&type_id_once, type_id);
    }
    return type_id_once;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

BobLauncherMatchRow *
bob_launcher_match_row_new(gint abs_index)
{
    BobLauncherMatchRow *self = g_object_new(BOB_LAUNCHER_TYPE_MATCH_ROW,
                                              "has-tooltip", TRUE,
                                              "overflow", GTK_OVERFLOW_HIDDEN,
                                              NULL);

    self->abs_index = abs_index;
    BobLauncherMatchRowPrivate *priv = self->priv;

    /* Create child widgets */
    gchar *title_classes[] = {"title", NULL};
    priv->title = bob_launcher_match_row_label_new(title_classes, 1);
    gtk_widget_set_parent(GTK_WIDGET(priv->title), GTK_WIDGET(self));

    gchar *desc_classes[] = {"description", NULL};
    priv->description = bob_launcher_match_row_label_new(desc_classes, 1);
    gtk_widget_set_parent(GTK_WIDGET(priv->description), GTK_WIDGET(self));

    priv->selected_row = bob_launcher_row_number_new(abs_index);
    gtk_widget_set_parent(priv->selected_row, GTK_WIDGET(self));

    priv->shortcut = GTK_LABEL(gtk_label_new(""));
    gtk_widget_add_css_class(GTK_WIDGET(priv->shortcut), SHORTCUT_CSS);
    gtk_widget_set_parent(GTK_WIDGET(priv->shortcut), GTK_WIDGET(self));

    /* Load highlight style */
    gchar *style_string = g_settings_get_string(match_row_settings, "highlight-style");
    priv->highlight_style = parse_highlight_style(style_string);
    g_free(style_string);

    /* Connect signals */
    g_signal_connect(match_row_settings, "changed::shortcut-indicator",
                     G_CALLBACK(on_settings_changed_update_ui), self);
    g_signal_connect(match_row_settings, "changed::match-description-next-to-title",
                     G_CALLBACK(on_settings_changed_update_ui), self);
    g_signal_connect(match_row_settings, "changed::match-icon-size",
                     G_CALLBACK(on_settings_changed_update_ui), self);
    g_signal_connect(match_row_settings, "changed::highlight-style",
                     G_CALLBACK(on_settings_changed_highlight_style), self);
    g_signal_connect_after(match_row_ui_settings, "accent-color-changed",
                           G_CALLBACK(on_accent_color_changed), self);
    g_signal_connect(self, "query-tooltip",
                     G_CALLBACK(on_query_tooltip_cb), self);

    bob_launcher_drag_and_drop_handler_setup(GTK_WIDGET(self), match_finder, self);
    update_ui(self);

    return self;
}

void
bob_launcher_match_row_update_match(BobLauncherMatchRow *self, needle_info *si)
{
    BobLauncherMatchRowPrivate *priv = self->priv;

    BobLauncherMatch *m = hashset_get_match_at(state_current_provider(), self->abs_index);
    if (m == NULL)
        return;

    /* Update title */
    g_free(priv->title_string);
    priv->title_string = bob_launcher_match_get_title(m);

    g_clear_pointer(&priv->title_positions, highlight_positions_free);
    priv->title_positions = highlight_calculate_positions(si, priv->title_string);

    const GdkRGBA *accent_color = highlight_get_accent_color();
    PangoAttrList *title_attrs = highlight_apply_style(priv->title_positions, priv->highlight_style, accent_color);
    bob_launcher_match_row_label_set_text(priv->title, priv->title_string, title_attrs);
    pango_attr_list_unref(title_attrs);

    /* Update description */
    priv->rich_description = BOB_LAUNCHER_IS_IRICH_DESCRIPTION(m)
        ? bob_launcher_irich_description_get_rich_description(BOB_LAUNCHER_IRICH_DESCRIPTION(m), si)
        : NULL;

    if (priv->rich_description == NULL) {
        g_free(priv->description_string);
        priv->description_string = bob_launcher_match_get_description(m);

        g_clear_pointer(&priv->description_positions, highlight_positions_free);
        priv->description_positions = highlight_calculate_positions(si, priv->description_string);

        PangoAttrList *desc_attrs = highlight_apply_style(priv->description_positions, priv->highlight_style, accent_color);
        bob_launcher_match_row_label_set_text(priv->description, priv->description_string, desc_attrs);
        pango_attr_list_unref(desc_attrs);
    } else {
        bob_launcher_match_row_label_set_description(priv->description, priv->rich_description);
        gtk_widget_add_css_class(GTK_WIDGET(priv->description), "description");

        g_clear_pointer(&priv->description_string, g_free);
        g_clear_pointer(&priv->description_positions, highlight_positions_free);
    }

    /* Update icon */
    if (priv->icon_widget != NULL) {
        gtk_widget_unparent(priv->icon_widget);
        priv->icon_widget = NULL;
    }

    if (BOB_LAUNCHER_IS_IRICH_ICON(m)) {
        priv->icon_widget = bob_launcher_irich_icon_get_rich_icon(BOB_LAUNCHER_IRICH_ICON(m));
        gtk_widget_set_parent(priv->icon_widget, GTK_WIDGET(self));
    } else {
        g_free(priv->icon_name);
        priv->icon_name = bob_launcher_match_get_icon_name(m);
    }

    gtk_widget_queue_draw(GTK_WIDGET(self));
}

void
bob_launcher_match_row_update(BobLauncherMatchRow *self,
                              needle_info *si,
                              gint new_row,
                              gint new_abs_index,
                              gboolean row_selected,
                              gint new_event)
{
    gint prev_abs_index = atomic_exchange(&self->abs_index, new_abs_index);
    gint prev_event = atomic_exchange(&self->event_id, new_event);

    if (prev_event != new_event || prev_abs_index != new_abs_index)
        bob_launcher_match_row_update_match(self, si);

    GtkStateFlags flag = row_selected ? GTK_STATE_FLAG_SELECTED : GTK_STATE_FLAG_NORMAL;
    gtk_widget_set_state_flags(GTK_WIDGET(self), flag, TRUE);

    bob_launcher_row_number_update_row_num(self->priv->selected_row, new_row);
}
