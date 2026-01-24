#include "main-container.h"
#include "match-row-label.h"
#include "bob-launcher.h"
#include <result-box.h>
#include <highlight.h>
#include <hashset.h>
#include <controller.h>
#include <graphene-gobject.h>
#include <gsk/gsk.h>
#include <gdk/gdk.h>

typedef struct _BobLauncherMatchRowPrivate BobLauncherMatchRowPrivate;
struct _BobLauncherMatchRow {
    GtkWidget parent_instance;
    BobLauncherMatchRowPrivate *priv;
    int abs_index;
    int event_id;
};

typedef void (*BobLauncherFragmentFunc)(gpointer user_data, GError **error);

typedef struct _BobLauncherQueryContainer BobLauncherQueryContainer;
typedef struct _BobLauncherResultBox BobLauncherResultBox;
typedef struct _BobLauncherLauncherWindow BobLauncherLauncherWindow;
typedef struct _BobLauncherMatchRow BobLauncherMatchRow;

#define BOB_LAUNCHER_TYPE_QUERY_CONTAINER (bob_launcher_query_container_get_type())
#define BOB_LAUNCHER_TYPE_RESULT_BOX (bob_launcher_result_box_get_type())
#define BOB_LAUNCHER_TYPE_MATCH_ROW (bob_launcher_match_row_get_type())
#define BOB_LAUNCHER_TYPE_LAUNCHER_WINDOW (bob_launcher_launcher_window_get_type())

#define BOB_LAUNCHER_MATCH_ROW(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), BOB_LAUNCHER_TYPE_MATCH_ROW, BobLauncherMatchRow))

typedef struct _BobLauncherProgressIndicatorWidget BobLauncherProgressIndicatorWidget;
typedef struct _BobLauncherProgressIndicatorWidgetClass BobLauncherProgressIndicatorWidgetClass;

struct _BobLauncherProgressIndicatorWidget {
    GtkWidget parent_instance;
};

struct _BobLauncherProgressIndicatorWidgetClass {
    GtkWidgetClass parent_class;
};

static gpointer bob_launcher_progress_indicator_widget_parent_class = NULL;

static void
bob_launcher_progress_indicator_widget_class_init(BobLauncherProgressIndicatorWidgetClass *klass,
                                                   gpointer klass_data)
{
    bob_launcher_progress_indicator_widget_parent_class = g_type_class_peek_parent(klass);
}

static void
bob_launcher_progress_indicator_widget_instance_init(BobLauncherProgressIndicatorWidget *self,
                                                      gpointer klass)
{
    gtk_widget_set_name(GTK_WIDGET(self), "progress-indicator");
}

static GType
bob_launcher_progress_indicator_widget_get_type(void)
{
    static gsize type_id = 0;
    if (g_once_init_enter(&type_id)) {
        static const GTypeInfo info = {
            sizeof(BobLauncherProgressIndicatorWidgetClass),
            NULL, NULL,
            (GClassInitFunc) bob_launcher_progress_indicator_widget_class_init,
            NULL, NULL,
            sizeof(BobLauncherProgressIndicatorWidget),
            0,
            (GInstanceInitFunc) bob_launcher_progress_indicator_widget_instance_init,
            NULL
        };
        GType id = g_type_register_static(GTK_TYPE_WIDGET,
                                          "BobLauncherProgressIndicatorWidget",
                                          &info, 0);
        g_once_init_leave(&type_id, id);
    }
    return type_id;
}

static BobLauncherProgressIndicatorWidget *
bob_launcher_progress_indicator_widget_new(void)
{
    return g_object_new(bob_launcher_progress_indicator_widget_get_type(), NULL);
}

struct _BobLauncherMainContainer {
    GtkWidget parent_instance;
};

struct _BobLauncherMainContainerClass {
    GtkWidgetClass parent_class;
};

static gpointer bob_launcher_main_container_parent_class = NULL;

static BobLauncherQueryContainer *qc = NULL;
static BobLauncherProgressIndicatorWidget *progress_indicator = NULL;
static BobLauncherResultBox *result_box = NULL;

static double fraction = 0.0;
static graphene_rect_t rect;
static int qc_height = 1;
static int box_height = 1;
static int bar_height = 1;

extern GType bob_launcher_query_container_get_type(void);
extern GType bob_launcher_result_box_get_type(void);
extern GType bob_launcher_match_row_get_type(void);
extern GType bob_launcher_launcher_window_get_type(void);

extern BobLauncherQueryContainer *bob_launcher_query_container_new(void);
extern BobLauncherResultBox *bob_launcher_result_box_new(void);
extern void bob_launcher_result_box_update_layout(BobLauncherResultBox *self,
                                                   HashSet *provider,
                                                   int selected_index);

extern BobLauncherLauncherWindow *bob_launcher_app_main_win;

static void bob_launcher_main_container_class_init(BobLauncherMainContainerClass *klass,
                                                    gpointer klass_data);
static void bob_launcher_main_container_instance_init(BobLauncherMainContainer *self,
                                                       gpointer klass);
static void bob_launcher_main_container_finalize(GObject *obj);
static GtkSizeRequestMode bob_launcher_main_container_get_request_mode(GtkWidget *widget);
static void bob_launcher_main_container_measure(GtkWidget *widget,
                                                 GtkOrientation orientation,
                                                 int for_size,
                                                 int *minimum,
                                                 int *natural,
                                                 int *minimum_baseline,
                                                 int *natural_baseline);
static void bob_launcher_main_container_size_allocate(GtkWidget *widget,
                                                       int width,
                                                       int height,
                                                       int baseline);
static void bob_launcher_main_container_snapshot(GtkWidget *widget,
                                                  GtkSnapshot *snapshot);
static void bob_launcher_main_container_setup_click_controller(BobLauncherMainContainer *self);

static GType
bob_launcher_main_container_get_type_once(void)
{
    static const GTypeInfo info = {
        sizeof(BobLauncherMainContainerClass),
        NULL, NULL,
        (GClassInitFunc) bob_launcher_main_container_class_init,
        NULL, NULL,
        sizeof(BobLauncherMainContainer),
        0,
        (GInstanceInitFunc) bob_launcher_main_container_instance_init,
        NULL
    };
    return g_type_register_static(GTK_TYPE_WIDGET, "BobLauncherMainContainer", &info, 0);
}

GType
bob_launcher_main_container_get_type(void)
{
    static volatile gsize type_id__once = 0;
    if (g_once_init_enter(&type_id__once)) {
        GType type_id = bob_launcher_main_container_get_type_once();
        g_once_init_leave(&type_id__once, type_id);
    }
    return type_id__once;
}

static void
bob_launcher_main_container_class_init(BobLauncherMainContainerClass *klass,
                                        gpointer klass_data)
{
    bob_launcher_main_container_parent_class = g_type_class_peek_parent(klass);

    G_OBJECT_CLASS(klass)->finalize = bob_launcher_main_container_finalize;

    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
    widget_class->get_request_mode = bob_launcher_main_container_get_request_mode;
    widget_class->measure = bob_launcher_main_container_measure;
    widget_class->size_allocate = bob_launcher_main_container_size_allocate;
    widget_class->snapshot = bob_launcher_main_container_snapshot;
}

static void
bob_launcher_main_container_instance_init(BobLauncherMainContainer *self,
                                           gpointer klass)
{
    graphene_rect_init(&rect, 0, 0, 0, 0);

    gtk_widget_set_overflow(GTK_WIDGET(self), GTK_OVERFLOW_HIDDEN);
    gtk_widget_set_name(GTK_WIDGET(self), "main-container");

    qc = bob_launcher_query_container_new();
    g_object_ref_sink(qc);
    gtk_widget_set_parent(GTK_WIDGET(qc), GTK_WIDGET(self));

    progress_indicator = bob_launcher_progress_indicator_widget_new();
    g_object_ref_sink(progress_indicator);
    gtk_widget_set_parent(GTK_WIDGET(progress_indicator), GTK_WIDGET(self));

    result_box = bob_launcher_result_box_new();
    g_object_ref_sink(result_box);
    gtk_widget_set_parent(GTK_WIDGET(result_box), GTK_WIDGET(self));

    bob_launcher_main_container_setup_click_controller(self);
}

static void
bob_launcher_main_container_finalize(GObject *obj)
{
    BobLauncherMainContainer *self = BOB_LAUNCHER_MAIN_CONTAINER(obj);

    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(self))) != NULL) {
        gtk_widget_unparent(child);
    }

    G_OBJECT_CLASS(bob_launcher_main_container_parent_class)->finalize(obj);
}

static GtkSizeRequestMode
bob_launcher_main_container_get_request_mode(GtkWidget *widget)
{
    return GTK_SIZE_REQUEST_CONSTANT_SIZE;
}

static void
bob_launcher_main_container_measure(GtkWidget *widget,
                                     GtkOrientation orientation,
                                     int for_size,
                                     int *minimum,
                                     int *natural,
                                     int *minimum_baseline,
                                     int *natural_baseline)
{
    *minimum_baseline = *natural_baseline = -1;
    *minimum = *natural = 0;

    if (orientation == GTK_ORIENTATION_VERTICAL) {
        gtk_widget_measure(GTK_WIDGET(qc), GTK_ORIENTATION_VERTICAL, -1,
                           NULL, &qc_height, NULL, NULL);
        gtk_widget_measure(GTK_WIDGET(progress_indicator), GTK_ORIENTATION_VERTICAL, -1,
                           NULL, &bar_height, NULL, NULL);
        gtk_widget_measure(GTK_WIDGET(result_box), GTK_ORIENTATION_VERTICAL, -1,
                           NULL, &box_height, NULL, NULL);
        *minimum = *natural = qc_height + box_height;
    } else {
        gtk_widget_measure(GTK_WIDGET(result_box), GTK_ORIENTATION_HORIZONTAL, -1,
                           NULL, NULL, NULL, NULL);
    }
}

static void
bob_launcher_main_container_size_allocate(GtkWidget *widget,
                                           int width,
                                           int height,
                                           int baseline)
{
    gtk_widget_allocate(GTK_WIDGET(qc), width, qc_height, baseline, NULL);

    graphene_point_t offset = GRAPHENE_POINT_INIT(0, qc_height);
    GskTransform *transform = gsk_transform_translate(NULL, &offset);

    gtk_widget_allocate(GTK_WIDGET(progress_indicator), width, bar_height, baseline,
                        gsk_transform_ref(transform));
    gtk_widget_allocate(GTK_WIDGET(result_box), width, box_height, baseline, transform);
}

static void
bob_launcher_main_container_snapshot(GtkWidget *widget,
                                      GtkSnapshot *snapshot)
{
    gtk_widget_snapshot_child(widget, GTK_WIDGET(qc), snapshot);
    gtk_widget_snapshot_child(widget, GTK_WIDGET(result_box), snapshot);

    const int width = gtk_widget_get_width(widget);
    const int progress_width = (int)(width * fraction);

    graphene_rect_init(&rect, 0, qc_height, progress_width, bar_height);

    gtk_snapshot_push_clip(snapshot, &rect);

    const GdkRGBA *accent_color = highlight_get_accent_color();
    gtk_snapshot_append_color(snapshot, accent_color, &rect);

    gtk_widget_snapshot_child(widget, GTK_WIDGET(progress_indicator), snapshot);

    gtk_snapshot_pop(snapshot);
}

BobLauncherMainContainer *
bob_launcher_main_container_new(void)
{
    return g_object_new(bob_launcher_main_container_get_type(), NULL);
}

void
bob_launcher_main_container_update_layout(HashSet *provider,
                                           int selected_index)
{
    fraction = (provider->size > 1)
        ? ((double)selected_index) / ((double)(provider->size - 1))
        : 0.0;

    bob_launcher_result_box_update_layout(result_box, provider, selected_index);
}

static void
bob_launcher_main_container_handle_click_release(BobLauncherMainContainer *self,
                                                  GtkGestureClick *controller,
                                                  int n_press,
                                                  double x,
                                                  double y)
{
    GtkWidget *picked_widget = gtk_widget_pick(GTK_WIDGET(result_box), x, y, GTK_PICK_DEFAULT);
    if (picked_widget == NULL)
        return;

    gboolean ctrl_pressed = FALSE;
    GdkDevice *device = gtk_gesture_get_device(GTK_GESTURE(controller));
    if (device != NULL) {
        GdkDisplay *display = gdk_device_get_display(device);
        GdkSeat *seat = gdk_display_get_default_seat(display);
        GdkDevice *keyboard = gdk_seat_get_keyboard(seat);
        if (keyboard != NULL) {
            GdkModifierType modifier_state = gdk_device_get_modifier_state(keyboard);
            ctrl_pressed = (modifier_state & GDK_CONTROL_MASK) != 0;
        }
    }

    /* Find the match row label that contains this widget */
    GtkWidget *label_widget = gtk_widget_get_ancestor(picked_widget, BOB_LAUNCHER_TYPE_MATCH_ROW_LABEL);
    if (label_widget != NULL) {
        BobLauncherMatchRowLabel *mrl = BOB_LAUNCHER_MATCH_ROW_LABEL(label_widget);
        ClickFunc func;
        void *target;

        if (bob_launcher_match_row_label_lookup_click(mrl, picked_widget, &func, &target)) {
            GError *error = NULL;
            func(target, &error);
            if (error != NULL) {
                g_warning("Failed to execute fragment action: %s", error->message);
                g_error_free(error);
            } else if (!ctrl_pressed) {
                gtk_widget_set_visible(GTK_WIDGET(bob_launcher_app_main_win), FALSE);
            }
            return;
        }
    }

    /* Fall through to match row handling */
    GtkWidget *item = gtk_widget_get_ancestor(picked_widget, BOB_LAUNCHER_TYPE_MATCH_ROW);
    if (item != NULL) {
        BobLauncherMatchRow *mr = BOB_LAUNCHER_MATCH_ROW(item);
        controller_goto_match_abs(mr->abs_index);
        controller_execute(!ctrl_pressed);
    }
}

static void
on_click_released(GtkGestureClick *gesture,
                  int n_press,
                  double x,
                  double y,
                  gpointer user_data)
{
    bob_launcher_main_container_handle_click_release(BOB_LAUNCHER_MAIN_CONTAINER(user_data),
                                                      gesture, n_press, x, y);
}

static void
bob_launcher_main_container_setup_click_controller(BobLauncherMainContainer *self)
{
    GtkGestureClick *click = GTK_GESTURE_CLICK(gtk_gesture_click_new());
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
    gtk_widget_add_controller(GTK_WIDGET(result_box), GTK_EVENT_CONTROLLER(click));
    g_signal_connect(click, "released", G_CALLBACK(on_click_released), self);
}
