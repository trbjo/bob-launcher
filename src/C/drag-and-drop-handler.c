#include "bob-launcher.h"
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gio/gio.h>
#include <controller.h>

/* ============================================================================
 * Type declarations
 * ============================================================================ */

typedef BobLauncherMatch *(*BobLauncherDragAndDropHandlerMatchFinderFunc)(
    gdouble x, gdouble y, gpointer user_data);

/* Forward declarations for types we only use as pointers */
typedef struct _BobLauncherFileMatch BobLauncherFileMatch;
typedef struct _BobLauncherIFile BobLauncherIFile;

/* Type macros */
#define BOB_LAUNCHER_TYPE_FILE_MATCH (bob_launcher_file_match_get_type())
#define BOB_LAUNCHER_IS_FILE_MATCH(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), BOB_LAUNCHER_TYPE_FILE_MATCH))
#define BOB_LAUNCHER_FILE_MATCH(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), BOB_LAUNCHER_TYPE_FILE_MATCH, BobLauncherFileMatch))

#define BOB_LAUNCHER_TYPE_IFILE (bob_launcher_ifile_get_type())
#define BOB_LAUNCHER_IFILE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), BOB_LAUNCHER_TYPE_IFILE, BobLauncherIFile))

/* ============================================================================
 * External function declarations
 * ============================================================================ */

extern GType bob_launcher_file_match_get_type(void);
extern GType bob_launcher_ifile_get_type(void);

extern gchar *bob_launcher_match_get_icon_name(BobLauncherMatch *self);
extern gchar *bob_launcher_ifile_get_uri(BobLauncherIFile *self);
extern gchar *bob_launcher_ifile_get_mime_type(BobLauncherIFile *self);
extern GBytes *bob_launcher_utils_load_file_content(const gchar *uri);

/* ============================================================================
 * Closure data for signal handlers
 * ============================================================================ */

typedef struct {
    gint ref_count;
    GtkDragSource *drag_source;
    GtkWidget *widget;
    BobLauncherDragAndDropHandlerMatchFinderFunc match_finder;
    gpointer match_finder_target;
    gboolean drop_accepted;
    gboolean ctrl_pressed;
} DragHandlerData;

static DragHandlerData *
drag_handler_data_ref(DragHandlerData *data)
{
    g_atomic_int_inc(&data->ref_count);
    return data;
}

static void
drag_handler_data_unref(gpointer user_data)
{
    DragHandlerData *data = user_data;
    if (g_atomic_int_dec_and_test(&data->ref_count)) {
        g_clear_object(&data->drag_source);
        g_clear_object(&data->widget);
        g_slice_free(DragHandlerData, data);
    }
}

/* ============================================================================
 * Signal handlers
 * ============================================================================ */

static GdkContentProvider *
on_prepare(GtkDragSource *source,
           gdouble x,
           gdouble y,
           gpointer user_data)
{
    DragHandlerData *data = user_data;

    /* Find match at coordinates */
    BobLauncherMatch *match = data->match_finder(x, y, data->match_finder_target);

    /* Only allow drag for file matches */
    if (!BOB_LAUNCHER_IS_FILE_MATCH(match)) {
        gtk_gesture_set_state(GTK_GESTURE(data->drag_source), GTK_EVENT_SEQUENCE_DENIED);
        return NULL;
    }

    gtk_gesture_set_state(GTK_GESTURE(data->drag_source), GTK_EVENT_SEQUENCE_CLAIMED);

    BobLauncherFileMatch *file_match = BOB_LAUNCHER_FILE_MATCH(match);
    BobLauncherIFile *ifile = BOB_LAUNCHER_IFILE(file_match);

    /* Set drag icon */
    GtkIconTheme *icon_theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
    gchar *icon_name = bob_launcher_match_get_icon_name(match);
    GtkIconPaintable *paintable = gtk_icon_theme_lookup_icon(
        icon_theme,
        icon_name,
        NULL,
        48,
        gtk_widget_get_scale_factor(data->widget),
        GTK_TEXT_DIR_NONE,
        GTK_ICON_LOOKUP_PRELOAD);
    g_free(icon_name);

    gtk_drag_source_set_icon(data->drag_source, GDK_PAINTABLE(paintable), 0, 0);
    g_object_unref(paintable);

    /* Get URI for content providers */
    gchar *uri = bob_launcher_ifile_get_uri(ifile);
    gsize uri_len = strlen(uri);

    /* Create URI provider */
    GBytes *uri_bytes = g_bytes_new(uri, uri_len);
    GdkContentProvider *uri_provider = gdk_content_provider_new_for_bytes("text/uri-list", uri_bytes);
    g_bytes_unref(uri_bytes);

    /* Create GNOME copied files provider */
    gchar *copy_str = g_strconcat("copy\n", uri, NULL);
    GBytes *copy_bytes = g_bytes_new(copy_str, strlen(copy_str));
    g_free(copy_str);
    GdkContentProvider *gnome_provider = gdk_content_provider_new_for_bytes(
        "application/x-gnome-copied-files", copy_bytes);
    g_bytes_unref(copy_bytes);

    /* Create text/plain provider with file path */
    GFile *file = g_file_new_for_uri(uri);
    gchar *path = g_file_get_path(file);
    g_object_unref(file);

    GBytes *path_bytes = g_bytes_new(path, strlen(path));
    g_free(path);
    GdkContentProvider *text_provider = gdk_content_provider_new_for_bytes("text/plain", path_bytes);
    g_bytes_unref(path_bytes);

    /* Check mime type for text content */
    gchar *mime_type = bob_launcher_ifile_get_mime_type(ifile);
    gboolean is_text = g_str_has_prefix(mime_type, "text");

    GdkContentProvider *result;

    if (is_text) {
        /* Include file content for text files */
        GBytes *content = bob_launcher_utils_load_file_content(uri);
        GdkContentProvider *file_provider = gdk_content_provider_new_for_bytes(mime_type, content);
        g_bytes_unref(content);

        GdkContentProvider *providers[] = {
            uri_provider,
            file_provider,
            gnome_provider,
            text_provider
        };
        result = gdk_content_provider_new_union(providers, 4);
    } else {
        GdkContentProvider *providers[] = {
            uri_provider,
            gnome_provider,
            text_provider
        };
        result = gdk_content_provider_new_union(providers, 3);
    }

    g_free(mime_type);
    g_free(uri);

    return result;
}

static void
on_drop_performed(GdkDrag *drag, gpointer user_data)
{
    DragHandlerData *data = user_data;
    data->drop_accepted = TRUE;
    gtk_event_controller_reset(GTK_EVENT_CONTROLLER(data->drag_source));
}

static void
on_dnd_finished(GdkDrag *drag, gpointer user_data)
{
    DragHandlerData *data = user_data;

    if (!data->ctrl_pressed && data->drop_accepted)
        controller_on_drag_and_drop_done();

    gtk_event_controller_reset(GTK_EVENT_CONTROLLER(data->drag_source));
    data->ctrl_pressed = FALSE;
    data->drop_accepted = FALSE;
}

static void
on_drag_begin(GtkDragSource *source,
              GdkDrag *drag,
              gpointer user_data)
{
    DragHandlerData *data = user_data;

    /* Check if Ctrl is pressed */
    GdkDevice *device = gdk_drag_get_device(drag);
    GdkDisplay *display = gdk_device_get_display(device);
    GdkSeat *seat = gdk_display_get_default_seat(display);
    GdkDevice *keyboard = gdk_seat_get_keyboard(seat);

    if (keyboard != NULL) {
        GdkModifierType modifier_state = gdk_device_get_modifier_state(keyboard);
        data->ctrl_pressed = (modifier_state & GDK_CONTROL_MASK) != 0;
    }

    data->drop_accepted = FALSE;

    /* Connect to drag signals */
    g_signal_connect_data(drag, "drop-performed",
                          G_CALLBACK(on_drop_performed),
                          drag_handler_data_ref(data),
                          (GClosureNotify)drag_handler_data_unref, 0);
    g_signal_connect_data(drag, "dnd-finished",
                          G_CALLBACK(on_dnd_finished),
                          drag_handler_data_ref(data),
                          (GClosureNotify)drag_handler_data_unref, 0);
}

static gboolean
on_drag_cancel(GtkDragSource *source,
               GdkDrag *drag,
               GdkDragCancelReason reason,
               gpointer user_data)
{
    return FALSE;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

void
bob_launcher_drag_and_drop_handler_setup(
    GtkWidget *widget,
    BobLauncherDragAndDropHandlerMatchFinderFunc match_finder,
    gpointer match_finder_target)
{
    g_return_if_fail(GTK_IS_WIDGET(widget));
    g_return_if_fail(match_finder != NULL);

    /* Create closure data */
    DragHandlerData *data = g_slice_new0(DragHandlerData);
    data->ref_count = 1;
    data->widget = g_object_ref(widget);
    data->match_finder = match_finder;
    data->match_finder_target = match_finder_target;
    data->drop_accepted = FALSE;
    data->ctrl_pressed = FALSE;

    /* Create and add drag source */
    data->drag_source = gtk_drag_source_new();
    gtk_widget_add_controller(widget, g_object_ref(GTK_EVENT_CONTROLLER(data->drag_source)));

    /* Connect signals */
    g_signal_connect_data(data->drag_source, "prepare",
                          G_CALLBACK(on_prepare),
                          drag_handler_data_ref(data),
                          (GClosureNotify)drag_handler_data_unref, 0);
    g_signal_connect_data(data->drag_source, "drag-begin",
                          G_CALLBACK(on_drag_begin),
                          drag_handler_data_ref(data),
                          (GClosureNotify)drag_handler_data_unref, 0);
    g_signal_connect(data->drag_source, "drag-cancel",
                     G_CALLBACK(on_drag_cancel), NULL);

    /* Release initial ref - signals hold their own refs */
    drag_handler_data_unref(data);
}
