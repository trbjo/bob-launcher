#include "bob-launcher.h"
#include "result-box.h"
#include <state.h>
#include <match.h>

typedef struct _BobLauncherMatchRowPrivate BobLauncherMatchRowPrivate;
struct _BobLauncherMatchRow {
    GtkWidget parent_instance;
    BobLauncherMatchRowPrivate *priv;
    gint abs_index;
    gint event_id;
};

typedef struct _BobLauncherUpDownResizeHandle BobLauncherUpDownResizeHandle;
typedef struct _BobLauncherResultBoxPrivate BobLauncherResultBoxPrivate;

struct _BobLauncherResultBox {
    GtkWidget parent_instance;
    BobLauncherResultBoxPrivate *priv;
};

struct _BobLauncherResultBoxClass {
    GtkWidgetClass parent_class;
};

struct _BobLauncherResultBoxPrivate {
    GSettings *ui_settings;
};

typedef struct _BobLauncherResultBoxSeparator BobLauncherResultBoxSeparator;
typedef struct _BobLauncherResultBoxSeparatorClass BobLauncherResultBoxSeparatorClass;

struct _BobLauncherResultBoxSeparator {
    GtkWidget parent_instance;
};

struct _BobLauncherResultBoxSeparatorClass {
    GtkWidgetClass parent_class;
};

static gint BobLauncherResultBox_private_offset;
static gpointer bob_launcher_result_box_parent_class = NULL;
static gpointer bob_launcher_result_box_separator_parent_class = NULL;
static gint *row_sizes;
static gint *sep_sizes;
static BobLauncherResultBoxSeparator **separators = NULL;
static gint separators_length1 = 0;

gint bob_launcher_result_box_box_size = 0;
gint bob_launcher_result_box_visible_size = 0;
BobLauncherMatchRow **bob_launcher_result_box_row_pool = NULL;
gint bob_launcher_result_box_row_pool_length1 = 0;

extern BobLauncherMatchRow *bob_launcher_match_row_new(gint abs_index);
extern void bob_launcher_match_row_update(BobLauncherMatchRow *self, needle_info *si,
                                          gint new_row, gint new_abs_index,
                                          gboolean row_selected, gint new_event);
extern void bob_launcher_main_container_update_layout(HashSet *provider, gint selected_index);
extern void bob_launcher_scroll_controller_setup(BobLauncherResultBox *result_box);
extern BobLauncherUpDownResizeHandle *bob_launcher_launcher_window_up_down_handle;

static void bob_launcher_result_box_class_init(BobLauncherResultBoxClass *klass, gpointer klass_data);
static void bob_launcher_result_box_instance_init(BobLauncherResultBox *self, gpointer klass);
static void bob_launcher_result_box_finalize(GObject *obj);
static void bob_launcher_result_box_initialize_slots(BobLauncherResultBox *self);

static inline gpointer
bob_launcher_result_box_get_instance_private(BobLauncherResultBox *self)
{
    return G_STRUCT_MEMBER_P(self, BobLauncherResultBox_private_offset);
}

static void
bob_launcher_result_box_separator_class_init(BobLauncherResultBoxSeparatorClass *klass, gpointer klass_data)
{
    bob_launcher_result_box_separator_parent_class = g_type_class_peek_parent(klass);
    gtk_widget_class_set_css_name(GTK_WIDGET_CLASS(klass), "matchrow-separator");
}

static GType
bob_launcher_result_box_separator_get_type(void)
{
    static gsize type_id = 0;
    if (g_once_init_enter(&type_id)) {
        static const GTypeInfo info = {
            sizeof(BobLauncherResultBoxSeparatorClass), NULL, NULL,
            (GClassInitFunc) bob_launcher_result_box_separator_class_init,
            NULL, NULL, sizeof(BobLauncherResultBoxSeparator), 0, NULL, NULL
        };
        GType id = g_type_register_static(GTK_TYPE_WIDGET, "BobLauncherResultBoxSeparator", &info, 0);
        g_once_init_leave(&type_id, id);
    }
    return type_id;
}

static BobLauncherResultBoxSeparator *
bob_launcher_result_box_separator_new(void)
{
    return (BobLauncherResultBoxSeparator *)g_object_new(bob_launcher_result_box_separator_get_type(), NULL);
}

static GType
bob_launcher_result_box_get_type_once(void)
{
    static const GTypeInfo info = {
        sizeof(BobLauncherResultBoxClass), NULL, NULL,
        (GClassInitFunc) bob_launcher_result_box_class_init, NULL, NULL,
        sizeof(BobLauncherResultBox), 0,
        (GInstanceInitFunc) bob_launcher_result_box_instance_init, NULL
    };
    GType type_id = g_type_register_static(GTK_TYPE_WIDGET, "BobLauncherResultBox", &info, 0);
    BobLauncherResultBox_private_offset = g_type_add_instance_private(type_id, sizeof(BobLauncherResultBoxPrivate));
    return type_id;
}

GType
bob_launcher_result_box_get_type(void)
{
    static volatile gsize type_id__once = 0;
    if (g_once_init_enter(&type_id__once)) {
        GType type_id = bob_launcher_result_box_get_type_once();
        g_once_init_leave(&type_id__once, type_id);
    }
    return type_id__once;
}

static void
bob_launcher_result_box_on_box_size_change(GSettings *settings, const gchar *key, gpointer user_data)
{
    BobLauncherResultBox *self = BOB_LAUNCHER_RESULT_BOX(user_data);
    bob_launcher_result_box_box_size = g_settings_get_int(settings, "box-size");

    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(self))) != NULL)
        gtk_widget_unparent(child);

    bob_launcher_result_box_initialize_slots(self);
    bob_launcher_main_container_update_layout(state_providers[state_sf], state_selected_indices[state_sf]);
    gtk_widget_queue_resize(GTK_WIDGET(self));
}

static void
bob_launcher_result_box_initialize_slots(BobLauncherResultBox *self)
{
    const gint size = bob_launcher_result_box_box_size;

    g_free(bob_launcher_result_box_row_pool);
    g_free(separators);
    g_free(row_sizes);
    g_free(sep_sizes);

    bob_launcher_result_box_row_pool = g_new0(BobLauncherMatchRow *, size);
    bob_launcher_result_box_row_pool_length1 = size;
    separators = g_new0(BobLauncherResultBoxSeparator *, size);
    separators_length1 = size;
    row_sizes = g_new0(gint, size);
    sep_sizes = g_new0(gint, size);

    for (gint i = 0; i < size; i++) {
        separators[i] = bob_launcher_result_box_separator_new();
        g_object_ref_sink(separators[i]);
        gtk_widget_set_parent(GTK_WIDGET(separators[i]), GTK_WIDGET(self));

        bob_launcher_result_box_row_pool[i] = bob_launcher_match_row_new(i);
        g_object_ref_sink(bob_launcher_result_box_row_pool[i]);
        gtk_widget_set_parent(GTK_WIDGET(bob_launcher_result_box_row_pool[i]), GTK_WIDGET(self));
    }
}

static GtkSizeRequestMode
bob_launcher_result_box_get_request_mode(GtkWidget *widget)
{
    return GTK_SIZE_REQUEST_CONSTANT_SIZE;
}

static inline gboolean
should_swap(BobLauncherMatchRow *a, BobLauncherMatchRow *b)
{
    return a->event_id < b->event_id || (a->event_id == b->event_id && a->abs_index > b->abs_index);
}

static void
sort_row_pool(void)
{
    BobLauncherMatchRow **pool = bob_launcher_result_box_row_pool;
    const gint len = bob_launcher_result_box_row_pool_length1;

    for (gint i = 1; i < len; i++) {
        BobLauncherMatchRow *key = pool[i];
        gint j = i - 1;
        while (j >= 0 && should_swap(pool[j], key)) {
            pool[j + 1] = pool[j];
            j--;
        }
        pool[j + 1] = key;
    }
}

void
bob_launcher_result_box_update_layout(BobLauncherResultBox *self, HashSet *provider, gint selected_index)
{
    BobLauncherMatchRow **pool = bob_launcher_result_box_row_pool;
    const gint box_size = bob_launcher_result_box_box_size;

    gtk_widget_set_visible(GTK_WIDGET(bob_launcher_launcher_window_up_down_handle), provider->size > 0);

    const gint old_visible = bob_launcher_result_box_visible_size;
    const gint provider_size = MAX(0, provider->size);
    const gint visible_size = MIN(provider_size, box_size);
    bob_launcher_result_box_visible_size = visible_size;

    if (visible_size == 0) {
        if (old_visible != 0)
            gtk_widget_queue_resize(GTK_WIDGET(self));
        return;
    }

    const gint before = (visible_size - 1) / 2;
    const gint start_index = MAX(0, MIN(provider_size - visible_size, selected_index - before));
    const gint stop_index = start_index + visible_size - 1;

    gint view_tail = stop_index;
    gint view_head = start_index;

    const gchar *query = state_get_query();
    gchar *stripped = g_strstrip(g_strdup(query));
    needle_info *si = prepare_needle(stripped);
    g_free(stripped);

    const gint event_id = provider->event_id;

    for (gint i = 0; i < visible_size; i++) {
        BobLauncherMatchRow *row = pool[i];
        gint abs_index;

        if (event_id != row->event_id) {
            abs_index = i + start_index;
        } else {
            const gint abs = row->abs_index;
            if (abs < start_index)
                abs_index = view_tail--;
            else if (abs > stop_index)
                abs_index = view_head++;
            else
                abs_index = abs;
        }

        bob_launcher_match_row_update(row, si, abs_index - start_index, abs_index, selected_index == abs_index, event_id);
    }

    free_string_info(si);
    sort_row_pool();

    const gint preceding = selected_index - start_index;
    const gint following = preceding + 1;

    for (gint i = 0; i < separators_length1; i++) {
        GtkStateFlags flag = (i == preceding || i == following) ? GTK_STATE_FLAG_SELECTED : GTK_STATE_FLAG_NORMAL;
        gtk_widget_set_state_flags(GTK_WIDGET(separators[i]), flag, TRUE);
    }

    if (old_visible != visible_size)
        gtk_widget_queue_resize(GTK_WIDGET(self));
    else
        gtk_widget_queue_allocate(GTK_WIDGET(self));
}

static void
bob_launcher_result_box_measure(GtkWidget *widget, GtkOrientation orientation, gint for_size,
                                gint *minimum, gint *natural, gint *minimum_baseline, gint *natural_baseline)
{
    *minimum_baseline = *natural_baseline = -1;
    *minimum = *natural = 0;

    const gint visible = bob_launcher_result_box_visible_size;
    BobLauncherMatchRow **pool = bob_launcher_result_box_row_pool;

    if (orientation == GTK_ORIENTATION_VERTICAL) {
        for (gint i = 0; i < visible; i++) {
            gint sep_nat, row_nat;
            gtk_widget_measure(GTK_WIDGET(separators[i]), GTK_ORIENTATION_VERTICAL, for_size, NULL, &sep_nat, NULL, NULL);
            sep_sizes[i] = sep_nat;
            gtk_widget_measure(GTK_WIDGET(pool[i]), GTK_ORIENTATION_VERTICAL, for_size, NULL, &row_nat, NULL, NULL);
            row_sizes[i] = row_nat;
            *natural += sep_nat + row_nat;
        }
    } else {
        for (gint i = 0; i < visible; i++) {
            gint row_nat;
            gtk_widget_measure(GTK_WIDGET(pool[i]), GTK_ORIENTATION_HORIZONTAL, -1, NULL, &row_nat, NULL, NULL);
            if (row_nat > *natural)
                *natural = row_nat;
        }
    }
}

static void
bob_launcher_result_box_size_allocate(GtkWidget *widget, gint width, gint height, gint baseline)
{
    const gint visible = bob_launcher_result_box_visible_size;
    BobLauncherMatchRow **pool = bob_launcher_result_box_row_pool;

    GskTransform *transform = NULL;
    graphene_point_t offset = GRAPHENE_POINT_INIT(0, 0);

    for (gint i = 0; i < visible; i++) {
        gtk_widget_allocate(GTK_WIDGET(separators[i]), width, sep_sizes[i], baseline,
                            transform ? gsk_transform_ref(transform) : NULL);
        offset.y = sep_sizes[i];
        transform = gsk_transform_translate(transform, &offset);

        gtk_widget_allocate(GTK_WIDGET(pool[i]), width, row_sizes[i], baseline, gsk_transform_ref(transform));
        offset.y = row_sizes[i];
        transform = gsk_transform_translate(transform, &offset);
    }

    gsk_transform_unref(transform);
}

static void
bob_launcher_result_box_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
    const gint visible = bob_launcher_result_box_visible_size;
    BobLauncherMatchRow **pool = bob_launcher_result_box_row_pool;

    for (gint i = 0; i < visible; i++)
        gtk_widget_snapshot_child(widget, GTK_WIDGET(separators[i]), snapshot);

    for (gint i = 0; i < visible; i++)
        gtk_widget_snapshot_child(widget, GTK_WIDGET(pool[i]), snapshot);
}

static void
bob_launcher_result_box_finalize(GObject *object)
{
    BobLauncherResultBox *self = BOB_LAUNCHER_RESULT_BOX(object);

    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(self))) != NULL)
        gtk_widget_unparent(child);

    g_clear_object(&self->priv->ui_settings);
    G_OBJECT_CLASS(bob_launcher_result_box_parent_class)->finalize(object);
}

static void
bob_launcher_result_box_class_init(BobLauncherResultBoxClass *klass, gpointer klass_data)
{
    bob_launcher_result_box_parent_class = g_type_class_peek_parent(klass);
    g_type_class_adjust_private_offset(klass, &BobLauncherResultBox_private_offset);

    G_OBJECT_CLASS(klass)->finalize = bob_launcher_result_box_finalize;
    GTK_WIDGET_CLASS(klass)->get_request_mode = bob_launcher_result_box_get_request_mode;
    GTK_WIDGET_CLASS(klass)->measure = bob_launcher_result_box_measure;
    GTK_WIDGET_CLASS(klass)->size_allocate = bob_launcher_result_box_size_allocate;
    GTK_WIDGET_CLASS(klass)->snapshot = bob_launcher_result_box_snapshot;
}

static void
bob_launcher_result_box_instance_init(BobLauncherResultBox *self, gpointer klass)
{
    self->priv = bob_launcher_result_box_get_instance_private(self);
    gtk_widget_set_name(GTK_WIDGET(self), "result-box");

    self->priv->ui_settings = g_settings_new(BOB_LAUNCHER_BOB_LAUNCHER_APP_ID ".ui");
    bob_launcher_result_box_box_size = g_settings_get_int(self->priv->ui_settings, "box-size");

    g_signal_connect(self->priv->ui_settings, "changed::box-size",
                     G_CALLBACK(bob_launcher_result_box_on_box_size_change), self);

    bob_launcher_result_box_initialize_slots(self);
    bob_launcher_scroll_controller_setup(self);
}

BobLauncherResultBox *
bob_launcher_result_box_new(void)
{
    return (BobLauncherResultBox *)g_object_new(bob_launcher_result_box_get_type(), NULL);
}
