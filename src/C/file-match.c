#include "file-match.h"
#include "bob-launcher.h"
#include "launch-context.h"
#include <icon-cache-service.h>
#include <gdk/gdk.h>
#include <pango/pango.h>
#include <string.h>

/* ============================================================================
 * Constants
 * ============================================================================ */

const gchar *BOB_LAUNCHER_FILE_MATCH_SEARCH_FILE_ATTRIBUTES =
    G_FILE_ATTRIBUTE_STANDARD_NAME ","
    G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
    G_FILE_ATTRIBUTE_STANDARD_DESCRIPTION ","
    G_FILE_ATTRIBUTE_STANDARD_TYPE ","
    G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
    G_FILE_ATTRIBUTE_STANDARD_SIZE ","
    G_FILE_ATTRIBUTE_STANDARD_ICON ","
    G_FILE_ATTRIBUTE_TIME_CREATED ","
    G_FILE_ATTRIBUTE_TIME_MODIFIED ","
    G_FILE_ATTRIBUTE_RECENT_MODIFIED ","
    G_FILE_ATTRIBUTE_UNIX_MODE ","
    G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK ","
    G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET ","
    G_FILE_ATTRIBUTE_OWNER_USER ","
    G_FILE_ATTRIBUTE_OWNER_GROUP ","
    G_FILE_ATTRIBUTE_TIME_ACCESS ","
    G_FILE_ATTRIBUTE_THUMBNAIL_PATH_XXLARGE ","
    G_FILE_ATTRIBUTE_THUMBNAILING_FAILED;

/* ============================================================================
 * Property IDs
 * ============================================================================ */

enum {
    PROP_0,
    PROP_FILENAME,
    PROP_TIMESTAMP,
    N_PROPERTIES
};

static GParamSpec *file_match_properties[N_PROPERTIES];

/* ============================================================================
 * External type declarations
 * ============================================================================ */

typedef struct _BobLauncherAppSettings BobLauncherAppSettings;
typedef struct _BobLauncherAppSettingsUI BobLauncherAppSettingsUI;
typedef struct _BobLauncherAppSettingsUIPrivate BobLauncherAppSettingsUIPrivate;
typedef struct _BobLauncherBobLaunchContext BobLauncherBobLaunchContext;
typedef struct _BobLauncherPaintableWidgetWrapper BobLauncherPaintableWidgetWrapper;

/* We need field access to AppSettingsUI for signals */
struct _BobLauncherAppSettingsUI {
    GTypeInstance parent_instance;
    volatile int ref_count;
    BobLauncherAppSettingsUIPrivate *priv;
    GSettings *settings;
};

/* ============================================================================
 * External function declarations (only for functions NOT in included headers)
 * ============================================================================ */

extern GType bob_launcher_app_settings_ui_get_type(void);
extern BobLauncherAppSettings *bob_launcher_app_settings_get_default(void);
extern BobLauncherAppSettingsUI *bob_launcher_app_settings_get_ui(BobLauncherAppSettings *self);
extern gpointer bob_launcher_app_settings_ui_ref(gpointer instance);
extern void bob_launcher_app_settings_ui_unref(gpointer instance);

// extern BobLauncherLaunchContext *bob_launcher_bob_launch_context_get_instance(void);
/* bob_launcher_bob_launch_context_launch_uri is in bob-launcher.h */

extern gchar *bob_launcher_utils_format_modification_time(GDateTime *now, GDateTime *modified);

extern GType bob_launcher_paintable_widget_wrapper_get_type(void);

/* Description API is in description.h */
/* Highlight API is in highlight.h */
/* Icon cache API is in icon-cache-service.h */

/* ============================================================================
 * Type definitions
 * ============================================================================ */

struct _BobLauncherFileMatchPrivate {
    GtkWidget *tooltip_widget;
    gchar *title;
    Description *description;
    GFile *file;
    GFileInfo *file_info;
    gchar *filename;
    GDateTime *timestamp;
    gchar *mime_type;
};

struct _BobLauncherFileMatch {
    BobLauncherMatch parent_instance;
    BobLauncherFileMatchPrivate *priv;
};

struct _BobLauncherFileMatchClass {
    BobLauncherMatchClass parent_class;
};

/* ============================================================================
 * Static variables
 * ============================================================================ */

static gint BobLauncherFileMatch_private_offset;
static gpointer bob_launcher_file_match_parent_class = NULL;

/* Class-level state */
static BobLauncherAppSettingsUI *file_match_ui_settings = NULL;
static HighlightStyle file_match_highlight_style = HIGHLIGHT_STYLE_COLOR;
static GHashTable *path_icon_cache = NULL;
static gchar **sorted_paths = NULL;
static gint sorted_paths_length = 0;

/* Interface parent pointers */
static BobLauncherIFileIface *file_match_ifile_parent_iface = NULL;
static BobLauncherIRichDescriptionIface *file_match_irich_description_parent_iface = NULL;

/* ============================================================================
 * Private function declarations
 * ============================================================================ */

static inline BobLauncherFileMatchPrivate *
bob_launcher_file_match_get_instance_private(BobLauncherFileMatch *self)
{
    return G_STRUCT_MEMBER_P(self, BobLauncherFileMatch_private_offset);
}

static void bob_launcher_file_match_class_init(BobLauncherFileMatchClass *klass, gpointer klass_data);
static void bob_launcher_file_match_instance_init(BobLauncherFileMatch *self, gpointer klass);
static void bob_launcher_file_match_ifile_interface_init(BobLauncherIFileIface *iface, gpointer data);
static void bob_launcher_file_match_irich_description_interface_init(BobLauncherIRichDescriptionIface *iface, gpointer data);
static void bob_launcher_file_match_dispose(GObject *obj);
static void bob_launcher_file_match_finalize(GObject *obj);
static GObject *bob_launcher_file_match_constructor(GType type, guint n_props, GObjectConstructParam *props);
static void bob_launcher_file_match_get_property(GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec);
static void bob_launcher_file_match_set_property(GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec);

/* Match virtual methods */
static GtkWidget *bob_launcher_file_match_get_tooltip(BobLauncherMatch *base);
static gchar *bob_launcher_file_match_get_title(BobLauncherMatch *base);
static gchar *bob_launcher_file_match_get_icon_name(BobLauncherMatch *base);
static gchar *bob_launcher_file_match_get_description_impl(BobLauncherMatch *base);

/* IFile interface methods */
static GFile *bob_launcher_file_match_ifile_get_file(BobLauncherIFile *iface);
static gchar *bob_launcher_file_match_ifile_get_file_path(BobLauncherIFile *iface);
static gboolean bob_launcher_file_match_ifile_is_directory(BobLauncherIFile *iface);
static gchar *bob_launcher_file_match_ifile_get_uri(BobLauncherIFile *iface);
static gchar *bob_launcher_file_match_ifile_get_mime_type(BobLauncherIFile *iface);

/* IRichDescription interface method */
static Description *bob_launcher_file_match_get_rich_description(BobLauncherIRichDescription *iface, needle_info *si);

/* Static helpers */
static void init_path_icon_cache(void);
static gboolean find_path_icon(const gchar *file_path, const gchar **matched_path, const gchar **matched_icon);
static HighlightStyle parse_highlight_style(const gchar *style);
static gint count_lines_in_file(GFile *file);

/* Tooltip handlers */
static void handle_image_tooltip(BobLauncherFileMatch *self, GtkBox *box, GFile *file, GFileInfo *file_info);
static void handle_text_tooltip(BobLauncherFileMatch *self, GtkBox *box, GFile *file, GFileInfo *file_info);
static void handle_audio_tooltip(BobLauncherFileMatch *self, GtkBox *box, GFile *file, GFileInfo *file_info);
static void handle_video_tooltip(BobLauncherFileMatch *self, GtkBox *box, GFile *file, GFileInfo *file_info);
static void handle_archive_tooltip(BobLauncherFileMatch *self, GtkBox *box, GFile *file, GFileInfo *file_info);
static void handle_generic_tooltip(BobLauncherFileMatch *self, GtkBox *box, GFile *file, GFileInfo *file_info, const gchar *mime_type);
static void add_size_and_time(GtkBox *box, GFileInfo *file_info);

/* ============================================================================
 * Type registration
 * ============================================================================ */

static GType
bob_launcher_file_match_get_type_once(void)
{
    static const GTypeInfo type_info = {
        sizeof(BobLauncherFileMatchClass),
        NULL, NULL,
        (GClassInitFunc)bob_launcher_file_match_class_init,
        NULL, NULL,
        sizeof(BobLauncherFileMatch),
        0,
        (GInstanceInitFunc)bob_launcher_file_match_instance_init,
        NULL
    };

    static const GInterfaceInfo ifile_info = {
        (GInterfaceInitFunc)bob_launcher_file_match_ifile_interface_init,
        NULL, NULL
    };

    static const GInterfaceInfo irich_desc_info = {
        (GInterfaceInitFunc)bob_launcher_file_match_irich_description_interface_init,
        NULL, NULL
    };

    GType type_id = g_type_register_static(BOB_LAUNCHER_TYPE_MATCH,
                                           "BobLauncherFileMatch",
                                           &type_info, 0);

    g_type_add_interface_static(type_id, BOB_LAUNCHER_TYPE_IFILE, &ifile_info);
    g_type_add_interface_static(type_id, BOB_LAUNCHER_TYPE_IRICH_DESCRIPTION, &irich_desc_info);

    BobLauncherFileMatch_private_offset =
        g_type_add_instance_private(type_id, sizeof(BobLauncherFileMatchPrivate));

    return type_id;
}

GType
bob_launcher_file_match_get_type(void)
{
    static volatile gsize type_id_once = 0;
    if (g_once_init_enter(&type_id_once)) {
        GType type_id = bob_launcher_file_match_get_type_once();
        g_once_init_leave(&type_id_once, type_id);
    }
    return type_id_once;
}

/* ============================================================================
 * Class initialization
 * ============================================================================ */

static void
on_highlight_style_changed(GSettings *settings, const gchar *key, gpointer user_data)
{
    gchar *style = g_settings_get_string(settings, "highlight-style");
    file_match_highlight_style = parse_highlight_style(style);
    g_free(style);
}

static void
bob_launcher_file_match_class_init(BobLauncherFileMatchClass *klass, gpointer klass_data)
{
    bob_launcher_file_match_parent_class = g_type_class_peek_parent(klass);
    g_type_class_adjust_private_offset(klass, &BobLauncherFileMatch_private_offset);

    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->constructor = bob_launcher_file_match_constructor;
    object_class->dispose = bob_launcher_file_match_dispose;
    object_class->finalize = bob_launcher_file_match_finalize;
    object_class->get_property = bob_launcher_file_match_get_property;
    object_class->set_property = bob_launcher_file_match_set_property;

    BobLauncherMatchClass *match_class = BOB_LAUNCHER_MATCH_CLASS(klass);
    match_class->get_tooltip = bob_launcher_file_match_get_tooltip;
    match_class->get_title = bob_launcher_file_match_get_title;
    match_class->get_icon_name = bob_launcher_file_match_get_icon_name;
    match_class->get_description = bob_launcher_file_match_get_description_impl;

    /* Install properties */
    file_match_properties[PROP_FILENAME] = g_param_spec_string(
        "filename", "filename", "filename",
        NULL, G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

    file_match_properties[PROP_TIMESTAMP] = g_param_spec_boxed(
        "timestamp", "timestamp", "timestamp",
        G_TYPE_DATE_TIME, G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE);

    g_object_class_install_properties(object_class, N_PROPERTIES, file_match_properties);

    /* Static class initialization */
    BobLauncherAppSettings *app_settings = bob_launcher_app_settings_get_default();
    file_match_ui_settings = bob_launcher_app_settings_get_ui(app_settings);

    GSettings *settings = g_settings_new(BOB_LAUNCHER_APP_ID ".ui");
    gchar *style_string = g_settings_get_string(settings, "highlight-style");
    file_match_highlight_style = parse_highlight_style(style_string);
    g_free(style_string);

    g_signal_connect(settings, "changed::highlight-style",
                     G_CALLBACK(on_highlight_style_changed), NULL);

    init_path_icon_cache();
}

static void
bob_launcher_file_match_ifile_interface_init(BobLauncherIFileIface *iface, gpointer data)
{
    file_match_ifile_parent_iface = g_type_interface_peek_parent(iface);
    iface->get_file = bob_launcher_file_match_ifile_get_file;
    iface->get_file_path = bob_launcher_file_match_ifile_get_file_path;
    iface->is_directory = bob_launcher_file_match_ifile_is_directory;
    iface->get_uri = bob_launcher_file_match_ifile_get_uri;
    iface->get_mime_type = bob_launcher_file_match_ifile_get_mime_type;
}

static void
bob_launcher_file_match_irich_description_interface_init(BobLauncherIRichDescriptionIface *iface, gpointer data)
{
    file_match_irich_description_parent_iface = g_type_interface_peek_parent(iface);
    iface->get_rich_description = bob_launcher_file_match_get_rich_description;
}

/* ============================================================================
 * Instance initialization
 * ============================================================================ */

static void
bob_launcher_file_match_instance_init(BobLauncherFileMatch *self, gpointer klass)
{
    self->priv = bob_launcher_file_match_get_instance_private(self);
    /* All pointers default to NULL */
}

static void
on_accent_color_changed(BobLauncherAppSettingsUI *ui, gpointer user_data)
{
    bob_launcher_file_match_rehighlight_matches(BOB_LAUNCHER_FILE_MATCH(user_data));
}

static GObject *
bob_launcher_file_match_constructor(GType type, guint n_props, GObjectConstructParam *props)
{
    GObjectClass *parent_class = G_OBJECT_CLASS(bob_launcher_file_match_parent_class);
    GObject *obj = parent_class->constructor(type, n_props, props);
    BobLauncherFileMatch *self = BOB_LAUNCHER_FILE_MATCH(obj);

    g_signal_connect_object(file_match_ui_settings, "accent-color-changed",
                            G_CALLBACK(on_accent_color_changed), self, 0);

    return obj;
}

/* ============================================================================
 * Dispose / Finalize
 * ============================================================================ */

static void
bob_launcher_file_match_dispose(GObject *obj)
{
    BobLauncherFileMatch *self = BOB_LAUNCHER_FILE_MATCH(obj);
    g_clear_object(&self->priv->tooltip_widget);
    G_OBJECT_CLASS(bob_launcher_file_match_parent_class)->dispose(obj);
}

static void
bob_launcher_file_match_finalize(GObject *obj)
{
    BobLauncherFileMatch *self = BOB_LAUNCHER_FILE_MATCH(obj);
    BobLauncherFileMatchPrivate *priv = self->priv;

    g_clear_object(&priv->tooltip_widget);
    g_free(priv->title);
    if (priv->description)
        description_free(priv->description);
    g_clear_object(&priv->file);
    g_clear_object(&priv->file_info);
    g_free(priv->filename);
    g_clear_pointer(&priv->timestamp, g_date_time_unref);
    g_free(priv->mime_type);

    G_OBJECT_CLASS(bob_launcher_file_match_parent_class)->finalize(obj);
}

/* ============================================================================
 * Property accessors
 * ============================================================================ */

static void
bob_launcher_file_match_get_property(GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec)
{
    BobLauncherFileMatch *self = BOB_LAUNCHER_FILE_MATCH(obj);

    switch (prop_id) {
        case PROP_FILENAME:
            g_value_set_string(value, self->priv->filename);
            break;
        case PROP_TIMESTAMP:
            g_value_set_boxed(value, bob_launcher_file_match_get_timestamp(self));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
            break;
    }
}

static void
bob_launcher_file_match_set_property(GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    BobLauncherFileMatch *self = BOB_LAUNCHER_FILE_MATCH(obj);

    switch (prop_id) {
        case PROP_FILENAME:
            g_free(self->priv->filename);
            self->priv->filename = g_value_dup_string(value);
            break;
        case PROP_TIMESTAMP:
            bob_launcher_file_match_set_timestamp(self, g_value_get_boxed(value));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
            break;
    }
}

const gchar *
bob_launcher_file_match_get_filename(BobLauncherFileMatch *self)
{
    g_return_val_if_fail(BOB_LAUNCHER_IS_FILE_MATCH(self), NULL);
    return self->priv->filename;
}

GDateTime *
bob_launcher_file_match_get_timestamp(BobLauncherFileMatch *self)
{
    g_return_val_if_fail(BOB_LAUNCHER_IS_FILE_MATCH(self), NULL);
    BobLauncherFileMatchPrivate *priv = self->priv;

    if (priv->timestamp == NULL) {
        GFileInfo *fi = bob_launcher_file_match_get_file_info(self);
        if (fi != NULL) {
            if (g_file_info_has_attribute(fi, G_FILE_ATTRIBUTE_TIME_ACCESS))
                priv->timestamp = g_file_info_get_access_date_time(fi);

            if (priv->timestamp == NULL && g_file_info_has_attribute(fi, G_FILE_ATTRIBUTE_TIME_MODIFIED))
                priv->timestamp = g_file_info_get_modification_date_time(fi);
        }
    }
    return priv->timestamp;
}

void
bob_launcher_file_match_set_timestamp(BobLauncherFileMatch *self, GDateTime *value)
{
    g_return_if_fail(BOB_LAUNCHER_IS_FILE_MATCH(self));

    g_clear_pointer(&self->priv->timestamp, g_date_time_unref);
    if (value)
        self->priv->timestamp = g_date_time_ref(value);

    g_object_notify_by_pspec(G_OBJECT(self), file_match_properties[PROP_TIMESTAMP]);
}

static GtkWidget *
bob_launcher_file_match_get_tooltip_old(BobLauncherMatch *base)
{
    BobLauncherFileMatch *self = BOB_LAUNCHER_FILE_MATCH(base);
    BobLauncherFileMatchPrivate *priv = self->priv;

    if (priv->tooltip_widget != NULL)
        return priv->tooltip_widget;

    GFile *file = bob_launcher_ifile_get_file(BOB_LAUNCHER_IFILE(self));
    if (!g_file_query_exists(file, NULL)) {
        g_object_unref(file);
        return NULL;
    }

    GtkBox *box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    g_object_ref_sink(box);

    gchar *mime_type = bob_launcher_ifile_get_mime_type(BOB_LAUNCHER_IFILE(self));

    GError *error = NULL;
    GFileInfo *file_info = g_file_query_info(file,
        BOB_LAUNCHER_FILE_MATCH_SEARCH_FILE_ATTRIBUTES,
        G_FILE_QUERY_INFO_NONE, NULL, &error);

    if (error != NULL) {
        gchar *msg = g_strdup_printf("Error reading file: %s", error->message);
        GtkLabel *error_label = GTK_LABEL(gtk_label_new(msg));
        g_object_ref_sink(error_label);
        gtk_label_set_xalign(error_label, 0);
        gtk_box_append(box, GTK_WIDGET(error_label));
        g_object_unref(error_label);
        g_free(msg);
        g_error_free(error);
    } else {
        gchar *basename = g_file_get_basename(file);
        GtkLabel *file_title = GTK_LABEL(gtk_label_new(basename));
        g_object_ref_sink(file_title);
        g_free(basename);
        gtk_widget_add_css_class(GTK_WIDGET(file_title), "tooltip-title");
        gtk_label_set_xalign(file_title, 0.5f);
        gtk_label_set_ellipsize(file_title, PANGO_ELLIPSIZE_MIDDLE);
        gtk_box_append(box, GTK_WIDGET(file_title));
        g_object_unref(file_title);

        if (g_str_has_prefix(mime_type, "image/")) {
            handle_image_tooltip(self, box, file, file_info);
        } else if (g_str_has_prefix(mime_type, "text/") ||
                   g_strcmp0(mime_type, "application/json") == 0 ||
                   g_strcmp0(mime_type, "application/xml") == 0) {
            handle_text_tooltip(self, box, file, file_info);
        } else if (g_str_has_prefix(mime_type, "audio/")) {
            handle_audio_tooltip(self, box, file, file_info);
        } else if (g_str_has_prefix(mime_type, "video/")) {
            handle_video_tooltip(self, box, file, file_info);
        } else if (g_str_has_prefix(mime_type, "application/zip") ||
                   g_str_has_prefix(mime_type, "application/x-tar")) {
            handle_archive_tooltip(self, box, file, file_info);
        } else {
            handle_generic_tooltip(self, box, file, file_info, mime_type);
        }

        g_object_unref(file_info);
    }

    g_free(mime_type);
    g_object_unref(file);

    priv->tooltip_widget = g_object_ref(GTK_WIDGET(box));
    g_object_unref(box);
    return priv->tooltip_widget;
}

static void
handle_image_tooltip(BobLauncherFileMatch *self, GtkBox *box, GFile *file, GFileInfo *file_info)
{
    GObject *obj = g_object_new(bob_launcher_paintable_widget_wrapper_get_type(),
                                 "file_info", file_info,
                                 "file", file,
                                 NULL);
    if (G_IS_INITIALLY_UNOWNED(obj))
        g_object_ref_sink(obj);

    if (obj != NULL) {
        gtk_box_append(box, GTK_WIDGET(obj));
        g_object_unref(obj);
    }
}

static void
handle_text_tooltip(BobLauncherFileMatch *self, GtkBox *box, GFile *file, GFileInfo *file_info)
{
    GError *error = NULL;
    GFileInputStream *input_stream = g_file_read(file, NULL, &error);

    if (error != NULL) {
        GtkLabel *error_label = GTK_LABEL(gtk_label_new("Cannot read text"));
        g_object_ref_sink(error_label);
        gtk_label_set_xalign(error_label, 0);
        gtk_box_append(box, GTK_WIDGET(error_label));
        g_object_unref(error_label);
        g_error_free(error);
        add_size_and_time(box, file_info);
        return;
    }

    guint8 buffer[1024];
    gssize bytes_read = g_input_stream_read(G_INPUT_STREAM(input_stream),
                                            buffer, sizeof(buffer) - 1, NULL, NULL);
    g_input_stream_close(G_INPUT_STREAM(input_stream), NULL, NULL);
    g_object_unref(input_stream);

    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        gchar **lines = g_strsplit((gchar *)buffer, "\n", -1);
        gint num_lines = g_strv_length(lines);
        gint preview_lines = MIN(num_lines, 6);

        GString *preview_text = g_string_new("");
        for (gint i = 0; i < preview_lines; i++) {
            gchar *line = lines[i];
            gsize len = strlen(line);

            if (len > 70) {
                gchar *truncated = g_strndup(line, 67);
                g_string_append(preview_text, truncated);
                g_string_append(preview_text, "...");
                g_free(truncated);
            } else {
                g_string_append(preview_text, line);
            }

            if (i < preview_lines - 1)
                g_string_append_c(preview_text, '\n');
        }
        g_strfreev(lines);

        GtkLabel *text_label = GTK_LABEL(gtk_label_new(preview_text->str));
        g_object_ref_sink(text_label);
        g_string_free(preview_text, TRUE);

        gtk_label_set_xalign(text_label, 0);
        gtk_label_set_yalign(text_label, 0);
        gtk_label_set_max_width_chars(text_label, 70);
        gtk_label_set_wrap(text_label, TRUE);
        gtk_label_set_use_markup(text_label, FALSE);
        gtk_widget_add_css_class(GTK_WIDGET(text_label), "monospace");
        gtk_box_append(box, GTK_WIDGET(text_label));
        g_object_unref(text_label);

        gint line_count = count_lines_in_file(file);
        if (line_count > 0) {
            gchar *lines_str = g_strdup_printf("%d lines", line_count);
            GtkLabel *lines_label = GTK_LABEL(gtk_label_new(lines_str));
            g_object_ref_sink(lines_label);
            g_free(lines_str);
            gtk_label_set_xalign(lines_label, 0);
            gtk_widget_add_css_class(GTK_WIDGET(lines_label), "dim-label");
            gtk_box_append(box, GTK_WIDGET(lines_label));
            g_object_unref(lines_label);
        }
    }

    add_size_and_time(box, file_info);
}

static void
handle_audio_tooltip(BobLauncherFileMatch *self, GtkBox *box, GFile *file, GFileInfo *file_info)
{
    GtkLabel *audio_label = GTK_LABEL(gtk_label_new("Audio file"));
    g_object_ref_sink(audio_label);
    gtk_label_set_xalign(audio_label, 0);
    gtk_box_append(box, GTK_WIDGET(audio_label));
    g_object_unref(audio_label);
    add_size_and_time(box, file_info);
}

static void
handle_video_tooltip(BobLauncherFileMatch *self, GtkBox *box, GFile *file, GFileInfo *file_info)
{
    GtkLabel *video_label = GTK_LABEL(gtk_label_new("Video file"));
    g_object_ref_sink(video_label);
    gtk_label_set_xalign(video_label, 0);
    gtk_box_append(box, GTK_WIDGET(video_label));
    g_object_unref(video_label);
    add_size_and_time(box, file_info);
}

static void
handle_archive_tooltip(BobLauncherFileMatch *self, GtkBox *box, GFile *file, GFileInfo *file_info)
{
    GtkLabel *archive_label = GTK_LABEL(gtk_label_new("Archive"));
    g_object_ref_sink(archive_label);
    gtk_label_set_xalign(archive_label, 0);
    gtk_box_append(box, GTK_WIDGET(archive_label));
    g_object_unref(archive_label);
    add_size_and_time(box, file_info);
}

static void
handle_generic_tooltip(BobLauncherFileMatch *self, GtkBox *box, GFile *file,
                       GFileInfo *file_info, const gchar *mime_type)
{
    GtkLabel *type_label = GTK_LABEL(gtk_label_new(mime_type));
    g_object_ref_sink(type_label);
    gtk_label_set_xalign(type_label, 0);
    gtk_box_append(box, GTK_WIDGET(type_label));
    g_object_unref(type_label);
    add_size_and_time(box, file_info);
}

static void
add_size_and_time(GtkBox *box, GFileInfo *file_info)
{
    if (g_file_info_has_attribute(file_info, G_FILE_ATTRIBUTE_STANDARD_SIZE)) {
        gint64 size = g_file_info_get_size(file_info);
        gchar *size_str = g_format_size(size);
        GtkLabel *size_label = GTK_LABEL(gtk_label_new(size_str));
        g_object_ref_sink(size_label);
        g_free(size_str);
        gtk_label_set_xalign(size_label, 0);
        gtk_box_append(box, GTK_WIDGET(size_label));
        g_object_unref(size_label);
    }

    GDateTime *time = NULL;
    if (g_file_info_has_attribute(file_info, G_FILE_ATTRIBUTE_TIME_ACCESS)) {
        time = g_file_info_get_access_date_time(file_info);
    } else if (g_file_info_has_attribute(file_info, G_FILE_ATTRIBUTE_TIME_MODIFIED)) {
        time = g_file_info_get_modification_date_time(file_info);
    }

    if (time != NULL) {
        GDateTime *now = g_date_time_new_now_local();
        gchar *formatted = bob_launcher_utils_format_modification_time(now, time);
        g_date_time_unref(now);
        g_date_time_unref(time);

        GtkLabel *time_label = GTK_LABEL(gtk_label_new(formatted));
        g_object_ref_sink(time_label);
        g_free(formatted);
        gtk_label_set_xalign(time_label, 0);
        gtk_box_append(box, GTK_WIDGET(time_label));
        g_object_unref(time_label);
    }
}

static GtkWidget *
bob_launcher_file_match_get_tooltip(BobLauncherMatch *base)
{
    BobLauncherFileMatch *self = BOB_LAUNCHER_FILE_MATCH(base);
    BobLauncherFileMatchPrivate *priv = self->priv;

    if (priv->tooltip_widget != NULL)
        return priv->tooltip_widget;

    GFile *file = bob_launcher_ifile_get_file(BOB_LAUNCHER_IFILE(self));
    if (!g_file_query_exists(file, NULL)) {
        g_object_unref(file);
        return NULL;
    }

    GtkBox *box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    g_object_ref_sink(box);

    gchar *mime_type = bob_launcher_ifile_get_mime_type(BOB_LAUNCHER_IFILE(self));

    GError *error = NULL;
    GFileInfo *file_info = g_file_query_info(file,
        BOB_LAUNCHER_FILE_MATCH_SEARCH_FILE_ATTRIBUTES,
        G_FILE_QUERY_INFO_NONE, NULL, &error);

    if (error != NULL) {
        gchar *msg = g_strdup_printf("Error reading file: %s", error->message);
        GtkLabel *error_label = GTK_LABEL(gtk_label_new(msg));
        g_object_ref_sink(error_label);
        gtk_label_set_xalign(error_label, 0);
        gtk_box_append(box, GTK_WIDGET(error_label));
        g_object_unref(error_label);
        g_free(msg);
        g_error_free(error);
    } else {
        gchar *basename = g_file_get_basename(file);
        GtkLabel *file_title = GTK_LABEL(gtk_label_new(basename));
        g_object_ref_sink(file_title);
        g_free(basename);
        gtk_widget_add_css_class(GTK_WIDGET(file_title), "tooltip-title");
        gtk_label_set_xalign(file_title, 0.5f);
        gtk_label_set_ellipsize(file_title, PANGO_ELLIPSIZE_MIDDLE);
        gtk_box_append(box, GTK_WIDGET(file_title));
        g_object_unref(file_title);

        if (g_str_has_prefix(mime_type, "image/")) {
            handle_image_tooltip(self, box, file, file_info);
        } else if (g_str_has_prefix(mime_type, "text/") ||
                   g_strcmp0(mime_type, "application/json") == 0 ||
                   g_strcmp0(mime_type, "application/xml") == 0) {
            handle_text_tooltip(self, box, file, file_info);
        } else if (g_str_has_prefix(mime_type, "audio/")) {
            handle_audio_tooltip(self, box, file, file_info);
        } else if (g_str_has_prefix(mime_type, "video/")) {
            handle_video_tooltip(self, box, file, file_info);
        } else if (g_str_has_prefix(mime_type, "application/zip") ||
                   g_str_has_prefix(mime_type, "application/x-tar")) {
            handle_archive_tooltip(self, box, file, file_info);
        } else {
            handle_generic_tooltip(self, box, file, file_info, mime_type);
        }

        g_object_unref(file_info);
    }

    g_free(mime_type);
    g_object_unref(file);

    priv->tooltip_widget = g_object_ref(GTK_WIDGET(box));
    g_object_unref(box);
    return priv->tooltip_widget;
}
static gchar *
bob_launcher_file_match_get_title(BobLauncherMatch *base)
{
    BobLauncherFileMatch *self = BOB_LAUNCHER_FILE_MATCH(base);
    BobLauncherFileMatchPrivate *priv = self->priv;

    if (priv->title == NULL)
        priv->title = g_path_get_basename(priv->filename);

    return g_strdup(priv->title);
}

static gchar *
bob_launcher_file_match_get_icon_name(BobLauncherMatch *base)
{
    BobLauncherFileMatch *self = BOB_LAUNCHER_FILE_MATCH(base);
    gchar *mime_type = bob_launcher_ifile_get_mime_type(BOB_LAUNCHER_IFILE(self));
    const gchar *icon = icon_cache_service_best_icon_name_for_mime_type(mime_type);
    gchar *result = g_strdup(icon);
    g_free(mime_type);
    return result;
}

static gchar *
bob_launcher_file_match_get_description_impl(BobLauncherMatch *base)
{
    g_assert_not_reached();
    return NULL;
}

/* ============================================================================
 * IFile interface implementation
 * ============================================================================ */

static GFile *
bob_launcher_file_match_ifile_get_file(BobLauncherIFile *iface)
{
    BobLauncherFileMatch *self = BOB_LAUNCHER_FILE_MATCH(iface);
    BobLauncherFileMatchPrivate *priv = self->priv;

    if (priv->file == NULL)
        priv->file = g_file_new_for_path(priv->filename);

    return g_object_ref(priv->file);
}

static gchar *
bob_launcher_file_match_ifile_get_file_path(BobLauncherIFile *iface)
{
    BobLauncherFileMatch *self = BOB_LAUNCHER_FILE_MATCH(iface);
    BobLauncherFileMatchPrivate *priv = self->priv;

    if (priv->file == NULL)
        priv->file = g_file_new_for_path(priv->filename);

    return g_file_get_path(priv->file);
}

static gboolean
bob_launcher_file_match_ifile_is_directory(BobLauncherIFile *iface)
{
    BobLauncherFileMatch *self = BOB_LAUNCHER_FILE_MATCH(iface);
    GFileInfo *fi = bob_launcher_file_match_get_file_info(self);
    return g_file_info_get_file_type(fi) == G_FILE_TYPE_DIRECTORY;
}

static gchar *
bob_launcher_file_match_ifile_get_uri(BobLauncherIFile *iface)
{
    BobLauncherFileMatch *self = BOB_LAUNCHER_FILE_MATCH(iface);
    BobLauncherFileMatchPrivate *priv = self->priv;

    if (priv->file == NULL)
        priv->file = g_file_new_for_path(priv->filename);

    return g_file_get_uri(priv->file);
}

static gchar *
bob_launcher_file_match_ifile_get_mime_type(BobLauncherIFile *iface)
{
    BobLauncherFileMatch *self = BOB_LAUNCHER_FILE_MATCH(iface);
    BobLauncherFileMatchPrivate *priv = self->priv;

    if (priv->mime_type == NULL) {
        priv->mime_type = g_strdup("application/x-unknown");
        GFileInfo *fi = bob_launcher_file_match_get_file_info(self);
        if (fi != NULL) {
            const gchar *content_type = g_file_info_get_content_type(fi);
            if (content_type != NULL) {
                g_free(priv->mime_type);
                priv->mime_type = g_content_type_get_mime_type(content_type);
            }
        }
    }
    return g_strdup(priv->mime_type);
}

/* Public method to get file info - caches result */
GFileInfo *
bob_launcher_file_match_get_file_info(BobLauncherFileMatch *self)
{
    g_return_val_if_fail(BOB_LAUNCHER_IS_FILE_MATCH(self), NULL);
    BobLauncherFileMatchPrivate *priv = self->priv;

    if (priv->file_info == NULL) {
        GFile *file = bob_launcher_ifile_get_file(BOB_LAUNCHER_IFILE(self));
        priv->file_info = g_file_query_info(file,
            BOB_LAUNCHER_FILE_MATCH_SEARCH_FILE_ATTRIBUTES,
            G_FILE_QUERY_INFO_NONE, NULL, NULL);
        g_object_unref(file);
    }
    return priv->file_info;
}

/* ============================================================================
 * IRichDescription interface implementation
 * ============================================================================ */

static Description *
bob_launcher_file_match_get_rich_description(BobLauncherIRichDescription *iface, needle_info *si)
{
    BobLauncherFileMatch *self = BOB_LAUNCHER_FILE_MATCH(iface);
    BobLauncherFileMatchPrivate *priv = self->priv;

    if (priv->description == NULL) {
        priv->description = bob_launcher_file_match_generate_description_for_file(
            si, priv->filename, bob_launcher_file_match_get_timestamp(self));
    }
    return priv->description;
}

void
bob_launcher_file_match_rehighlight_matches(BobLauncherFileMatch *self)
{
    g_return_if_fail(BOB_LAUNCHER_IS_FILE_MATCH(self));

    if (self->priv->description != NULL) {
        description_free(self->priv->description);
        self->priv->description = NULL;
    }
}

/* ============================================================================
 * Path icon cache
 * ============================================================================ */

static gint
compare_path_lengths(gconstpointer a, gconstpointer b)
{
    const gchar *path_a = *(const gchar **)a;
    const gchar *path_b = *(const gchar **)b;
    return (gint)strlen(path_b) - (gint)strlen(path_a);
}

static void
init_path_icon_cache(void)
{
    path_icon_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    static const GUserDirectory DIRS[] = {
        G_USER_DIRECTORY_DOCUMENTS,
        G_USER_DIRECTORY_DOWNLOAD,
        G_USER_DIRECTORY_MUSIC,
        G_USER_DIRECTORY_PICTURES,
        G_USER_DIRECTORY_PUBLIC_SHARE,
        G_USER_DIRECTORY_TEMPLATES,
        G_USER_DIRECTORY_VIDEOS
    };

    static const gchar *ICONS[] = {
        "folder-documents-symbolic",
        "folder-download-symbolic",
        "folder-music-symbolic",
        "folder-pictures-symbolic",
        "folder-publicshare-symbolic",
        "folder-templates-symbolic",
        "folder-videos-symbolic"
    };

    for (gsize i = 0; i < G_N_ELEMENTS(DIRS); i++) {
        const gchar *path = g_get_user_special_dir(DIRS[i]);
        if (path != NULL)
            g_hash_table_insert(path_icon_cache, g_strdup(path), (gpointer)ICONS[i]);
    }

    g_hash_table_insert(path_icon_cache, g_strdup(g_get_home_dir()),
                        (gpointer)"user-home-symbolic");

    /* Build sorted paths array (longest first) */
    GList *keys = g_hash_table_get_keys(path_icon_cache);
    sorted_paths_length = g_list_length(keys);
    sorted_paths = g_new(gchar *, sorted_paths_length);

    gint i = 0;
    for (GList *l = keys; l != NULL; l = l->next)
        sorted_paths[i++] = g_strdup(l->data);

    g_list_free(keys);

    qsort(sorted_paths, sorted_paths_length, sizeof(gchar *), compare_path_lengths);
}

static gboolean
find_path_icon(const gchar *file_path, const gchar **matched_path, const gchar **matched_icon)
{
    *matched_path = NULL;
    *matched_icon = NULL;

    for (gint i = 0; i < sorted_paths_length; i++) {
        const gchar *path = sorted_paths[i];
        if (g_str_has_prefix(file_path, path)) {
            *matched_path = path;
            *matched_icon = g_hash_table_lookup(path_icon_cache, path);
            return TRUE;
        }
    }
    return FALSE;
}

static HighlightStyle
parse_highlight_style(const gchar *style)
{
    if (g_strcmp0(style, "underline") == 0)
        return HIGHLIGHT_STYLE_UNDERLINE;
    if (g_strcmp0(style, "bold") == 0)
        return HIGHLIGHT_STYLE_BOLD;
    if (g_strcmp0(style, "background") == 0)
        return HIGHLIGHT_STYLE_BACKGROUND;
    if (g_strcmp0(style, "bold-underline") == 0)
        return HIGHLIGHT_STYLE_BOLD | HIGHLIGHT_STYLE_UNDERLINE;
    return HIGHLIGHT_STYLE_COLOR;
}

/* ============================================================================
 * Description generation
 * ============================================================================ */

GPtrArray *
bob_launcher_file_match_split_path_with_separators(const gchar *path)
{
    GPtrArray *components = g_ptr_array_new_with_free_func(g_free);
    gchar **parts = g_strsplit(path, G_DIR_SEPARATOR_S, -1);

    /* Skip root (index 0) */
    for (gint i = 1; parts[i] != NULL; i++) {
        g_ptr_array_add(components, g_strdup(G_DIR_SEPARATOR_S));
        g_ptr_array_add(components, g_strdup(parts[i]));
    }

    g_strfreev(parts);
    return components;
}

/* Closure data for click callbacks */
typedef struct {
    gchar *uri;
} ClickData;

static void
click_data_free(gpointer data)
{
    ClickData *cd = data;
    g_free(cd->uri);
    g_slice_free(ClickData, cd);
}

static void
launch_uri_callback(gpointer user_data, GError **error)
{
    (void)error;  /* unused */
    ClickData *data = user_data;
    bob_launcher_bob_launch_context_launch_uri(
        bob_launcher_bob_launch_context_get_instance(), data->uri);
}

static void
launch_root_callback(gpointer user_data, GError **error)
{
    (void)user_data;  /* unused */
    (void)error;      /* unused */
    bob_launcher_bob_launch_context_launch_uri(
        bob_launcher_bob_launch_context_get_instance(), "file:///");
}

Description *
bob_launcher_file_match_generate_description_for_file(needle_info *si,
                                                       const gchar *file_path,
                                                       GDateTime *timestamp)
{
    Description *root = description_new_container("file-description", NULL, NULL, NULL);

    /* Timestamp group */
    if (timestamp != NULL) {
        Description *timestamp_group = description_new_container("timestamp-group", NULL, NULL, NULL);

        ImageDesc *separator = description_new_image("tools-timer-symbolic", "timestamp-image",
                                                      NULL, NULL, NULL);
        description_add_image(timestamp_group, separator);

        GDateTime *now = g_date_time_new_now_local();
        gchar *formatted_time = bob_launcher_utils_format_modification_time(now, timestamp);
        g_date_time_unref(now);

        TextDesc *time_desc = description_new_text(formatted_time, "timestamp",
                                                    NULL, NULL, NULL, NULL);
        g_free(formatted_time);
        description_add_text(timestamp_group, time_desc);

        description_add_container(root, timestamp_group);
    }

    /* Path group */
    Description *path_group = description_new_container("path-group", NULL, NULL, NULL);

    GPtrArray *components = bob_launcher_file_match_split_path_with_separators(file_path);
    GString *path_builder = g_string_new("");
    GdkRGBA *accent_color = highlight_get_accent_color();

    size_t byte_offset = 0;
    HighlightPositions *positions = highlight_calculate_positions(si, file_path);

    const gchar *matched_path = NULL;
    const gchar *matched_icon = NULL;

    if (find_path_icon(file_path, &matched_path, &matched_icon)) {
        /* Consume path components up to matched_path */
        while (path_builder->len < strlen(matched_path) && components->len > 0) {
            gchar *comp = g_ptr_array_steal_index(components, 0);
            g_string_append(path_builder, comp);
            g_free(comp);
        }
        byte_offset = path_builder->len;

        ClickData *cd = g_slice_new(ClickData);
        cd->uri = g_strconcat("file://", path_builder->str, NULL);

        ImageDesc *icon = description_new_image(matched_icon, "image",
                                                 launch_uri_callback, cd, click_data_free);
        description_add_image(path_group, icon);
    } else {
        ImageDesc *root_icon = description_new_image("drive-harddisk-symbolic", "image",
                                                      launch_root_callback, NULL, NULL);
        description_add_image(path_group, root_icon);
    }

    size_t current_byte_pos = byte_offset;

    for (guint i = 0; i < components->len; i++) {
        const gchar *component = g_ptr_array_index(components, i);
        g_string_append(path_builder, component);
        size_t comp_len = strlen(component);

        if (g_strcmp0(component, G_DIR_SEPARATOR_S) == 0) {
            ImageDesc *sep = description_new_image("path-separator-symbolic", "image",
                                                    NULL, NULL, NULL);
            description_add_image(path_group, sep);
        } else {
            PangoAttrList *attrs = highlight_apply_style_range(positions,
                file_match_highlight_style, accent_color,
                current_byte_pos, current_byte_pos + comp_len);

            ClickData *cd = g_slice_new(ClickData);
            cd->uri = g_strconcat("file://", path_builder->str, NULL);

            TextDesc *text = description_new_text(component, "path-fragment", attrs,
                                                   launch_uri_callback, cd, click_data_free);
            if (attrs)
                pango_attr_list_unref(attrs);
            description_add_text(path_group, text);
        }
        current_byte_pos += comp_len;
    }

    description_add_container(root, path_group);

    highlight_positions_free(positions);
    g_string_free(path_builder, TRUE);
    g_ptr_array_unref(components);

    return root;
}

static gint
count_lines_in_file(GFile *file)
{
    GError *error = NULL;
    GFileInputStream *input_stream = g_file_read(file, NULL, &error);
    if (error != NULL) {
        g_error_free(error);
        return -1;
    }

    GDataInputStream *data_stream = g_data_input_stream_new(G_INPUT_STREAM(input_stream));
    gint line_count = 0;
    gchar *line;

    while ((line = g_data_input_stream_read_line(data_stream, NULL, NULL, NULL)) != NULL) {
        g_free(line);
        line_count++;
        if (line_count > 5000)
            break;
    }

    g_input_stream_close(G_INPUT_STREAM(data_stream), NULL, NULL);
    g_object_unref(data_stream);
    g_input_stream_close(G_INPUT_STREAM(input_stream), NULL, NULL);
    g_object_unref(input_stream);

    return line_count;
}

/* ============================================================================
 * Constructors
 * ============================================================================ */

BobLauncherFileMatch *
bob_launcher_file_match_new_from_path(const gchar *filename)
{
    return g_object_new(BOB_LAUNCHER_TYPE_FILE_MATCH,
                        "filename", filename,
                        NULL);
}

BobLauncherFileMatch *
bob_launcher_file_match_new_from_uri(const gchar *uri)
{
    GError *error = NULL;
    gchar *filename = g_filename_from_uri(uri, NULL, &error);

    if (error != NULL) {
        g_warning("could not resolve uri: %s, error: %s", uri, error->message);
        g_error_free(error);
        return NULL;
    }

    BobLauncherFileMatch *result = g_object_new(BOB_LAUNCHER_TYPE_FILE_MATCH,
                                                 "filename", filename,
                                                 NULL);
    g_free(filename);
    return result;
}
