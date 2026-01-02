#include "scroll-controller.h"
#include <gtk/gtk.h>
#include <math.h>
#include <stdatomic.h>

typedef struct _BobLauncherResultBox BobLauncherResultBox;
typedef struct _BobLauncherMatchRow BobLauncherMatchRow;
typedef struct _BobLauncherLauncherWindow BobLauncherLauncherWindow;

extern BobLauncherLauncherWindow *bob_launcher_app_main_win;
extern BobLauncherMatchRow **bob_launcher_result_box_row_pool;
extern void controller_goto_match(int delta);
extern void state_update_layout(int searching_for);

#define SEARCHING_FOR_CURRENT -1

static atomic_uint scroll_tick_id = 0;
static double remaining_velocity = 0.0;
static double accumulated_scroll = 0.0;
static double initial_velocity = 0.0;
static double scroll_accumulator = 0.0;
static gboolean scrolling_down = TRUE;
static double current_item_height = 0.0;

void
bob_launcher_scroll_controller_reset(void)
{
    guint prev = atomic_exchange(&scroll_tick_id, 0);
    if (prev != 0) {
        gtk_widget_remove_tick_callback(GTK_WIDGET(bob_launcher_app_main_win), prev);
    }
    scroll_accumulator = 0.0;
    remaining_velocity = 0.0;
    accumulated_scroll = 0.0;
}

static gboolean
tick_callback(GtkWidget *widget, GdkFrameClock *frame_clock, gpointer user_data)
{
    (void)widget;
    (void)frame_clock;
    (void)user_data;

    double velocity_ratio = remaining_velocity / current_item_height;
    double items_to_scroll = log10(velocity_ratio);

    if (items_to_scroll < 0.45) {
        accumulated_scroll = 0.0;
        remaining_velocity = 0.0;
        initial_velocity = 0.0;
        atomic_store(&scroll_tick_id, 0);
        return FALSE;
    }

    if (items_to_scroll > 1.0 || scroll_accumulator > 1.0) {
        int items = (int)round(items_to_scroll);
        if (items < 1) items = 1;
        controller_goto_match(scrolling_down ? items : -items);
        remaining_velocity -= current_item_height * items;
        scroll_accumulator = 0.0;
        state_update_layout(SEARCHING_FOR_CURRENT);
    } else {
        scroll_accumulator += pow(scroll_accumulator, 4.0) + pow(items_to_scroll - floor(items_to_scroll), 4.0);
    }

    return TRUE;
}

static gboolean
handle_scroll(GtkEventControllerScroll *controller, double dx, double dy, gpointer user_data)
{
    (void)controller;
    (void)dx;
    (void)user_data;

    gboolean local_direction = dy > 0;

    if (local_direction != scrolling_down) {
        scrolling_down = local_direction;
        guint prev = atomic_exchange(&scroll_tick_id, 0);
        if (prev != 0) {
            gtk_widget_remove_tick_callback(GTK_WIDGET(bob_launcher_app_main_win), prev);
        }
        accumulated_scroll = 0.0;
        remaining_velocity = 0.0;
        initial_velocity = 0.0;
    }

    accumulated_scroll += dy;
    current_item_height = (double)gtk_widget_get_height(GTK_WIDGET(bob_launcher_result_box_row_pool[0]));

    if (accumulated_scroll >= current_item_height) {
        controller_goto_match(1);
        accumulated_scroll -= current_item_height;
    }

    if (accumulated_scroll <= -current_item_height) {
        controller_goto_match(-1);
        accumulated_scroll += current_item_height;
    }

    state_update_layout(SEARCHING_FOR_CURRENT);
    return TRUE;
}

static void
handle_decelerate(GtkEventControllerScroll *controller, double vel_x, double vel_y, gpointer user_data)
{
    (void)controller;
    (void)vel_x;
    (void)user_data;

    initial_velocity += fabs(vel_y);
    remaining_velocity += fabs(vel_y);

    guint expected = 0;
    if (atomic_compare_exchange_strong(&scroll_tick_id, &expected, 1)) {
        guint id = gtk_widget_add_tick_callback(GTK_WIDGET(bob_launcher_app_main_win), tick_callback, NULL, NULL);
        atomic_store(&scroll_tick_id, id);
    }
}

void
bob_launcher_scroll_controller_setup(BobLauncherResultBox *result_box)
{
    GtkEventControllerScroll *scroll = GTK_EVENT_CONTROLLER_SCROLL(
        gtk_event_controller_scroll_new(
            GTK_EVENT_CONTROLLER_SCROLL_VERTICAL | GTK_EVENT_CONTROLLER_SCROLL_KINETIC));

    gtk_widget_add_controller(GTK_WIDGET(result_box), GTK_EVENT_CONTROLLER(scroll));

    g_signal_connect(scroll, "scroll", G_CALLBACK(handle_scroll), NULL);
    g_signal_connect(scroll, "decelerate", G_CALLBACK(handle_decelerate), NULL);
}
