#include "css.h"
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <stdio.h>

#define BOB_LAUNCHER_APP_ID "io.github.trbjo.bob.launcher"

typedef struct _BobLauncherLauncherWindow BobLauncherLauncherWindow;

extern BobLauncherLauncherWindow *bob_launcher_app_main_win;
extern void input_region_reset(void);

static const char *color_variables =
    "\n"
    "@define-color base_transparent       alpha(@theme_base_color, %.2f);\n"
    "@define-color bg_transparent         alpha(@theme_bg_color, %.2f);\n"
    "\n"
    "@define-color separator_color             alpha(mix(@theme_base_color, #000, 0.23), %.2f);\n"
    "@define-color unfocused_fg_color          mix(@theme_base_color, @theme_text_color, 0.5);\n"
    "@define-color selected_match_row          alpha(mix(@theme_bg_color, @theme_text_color, 0.12), %.2f);\n";

static const char *color_variables_opaque =
    "\n"
    "@define-color base_transparent       @theme_base_color;\n"
    "@define-color bg_transparent         @theme_bg_color;\n"
    "\n"
    "@define-color unfocused_fg_color          mix(@theme_base_color, @theme_text_color, 0.5);\n"
    "@define-color separator_color             mix(@theme_base_color, #000, 0.23);\n"
    "@define-color selected_match_row          mix(@theme_base_color, @theme_text_color, 0.12);\n"
    "\n"
    "#result-box {\n"
    "    background: @base_transparent;\n"
    "}\n"
    "\n";

static GSettings *settings = NULL;
static GFileMonitor *file_monitor = NULL;
static GtkCssProvider *css_provider = NULL;
static GtkCssProvider *opacity_provider = NULL;

static void css_set_css_opacity(void);
static void css_update_css_sheet(void);
static void css_unload_css(void);
static void css_load_default_css(GtkCssProvider *provider);
static void css_on_css_file_changed(GFileMonitor *monitor, GFile *file, GFile *other_file,
                                     GFileMonitorEvent event_type, gpointer user_data);

static void
on_opacity_changed(GSettings *gsettings, const char *key, gpointer user_data)
{
    (void)gsettings;
    (void)key;
    (void)user_data;
    css_set_css_opacity();
}

static void
on_css_sheet_changed(GSettings *gsettings, const char *key, gpointer user_data)
{
    (void)gsettings;
    (void)key;
    (void)user_data;
    css_update_css_sheet();
}

void
css_initialize(void)
{
    opacity_provider = gtk_css_provider_new();
    settings = g_settings_new(BOB_LAUNCHER_APP_ID ".ui");

    css_set_css_opacity();
    g_signal_connect(settings, "changed::opacity", G_CALLBACK(on_opacity_changed), NULL);

    css_update_css_sheet();
    g_signal_connect(settings, "changed::css-sheet", G_CALLBACK(on_css_sheet_changed), NULL);
}

static void
css_set_css_opacity(void)
{
    double opacity = g_settings_get_double(settings, "opacity");

    GdkDisplay *display = gdk_display_get_default();
    gtk_style_context_remove_provider_for_display(display, GTK_STYLE_PROVIDER(opacity_provider));

    char *alpha_colors;
    if (opacity == 1.0) {
        alpha_colors = g_strdup(color_variables_opaque);
    } else {
        alpha_colors = g_strdup_printf(color_variables, opacity, opacity, opacity, opacity);
    }

    gtk_css_provider_load_from_string(opacity_provider, alpha_colors);
    gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(opacity_provider),
                                                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    g_free(alpha_colors);
}

static void
css_unload_css(void)
{
    if (css_provider != NULL) {
        GdkDisplay *display = gdk_display_get_default();
        gtk_style_context_remove_provider_for_display(display, GTK_STYLE_PROVIDER(css_provider));
        g_object_unref(css_provider);
        css_provider = NULL;
    }
    css_provider = gtk_css_provider_new();
}

static void
css_update_css_sheet(void)
{
    if (file_monitor != NULL) {
        g_signal_handlers_disconnect_by_func(file_monitor, css_on_css_file_changed, NULL);
        g_file_monitor_cancel(file_monitor);
        g_object_unref(file_monitor);
        file_monitor = NULL;
    }

    css_unload_css();

    char *css_path = g_settings_get_string(settings, "css-sheet");

    if (g_file_test(css_path, G_FILE_TEST_EXISTS)) {
        gtk_css_provider_load_from_path(css_provider, css_path);

        GFile *file = g_file_new_for_path(css_path);
        GError *error = NULL;
        file_monitor = g_file_monitor_file(file, G_FILE_MONITOR_NONE, NULL, &error);
        g_object_unref(file);

        if (error != NULL) {
            g_warning("Error setting up CSS file monitor: %s", error->message);
            g_error_free(error);
            css_unload_css();
            css_load_default_css(css_provider);
        } else {
            g_signal_connect(file_monitor, "changed", G_CALLBACK(css_on_css_file_changed), NULL);
        }
    } else {
        css_load_default_css(css_provider);
    }

    GdkDisplay *display = gdk_display_get_default();
    gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(css_provider),
                                                GTK_STYLE_PROVIDER_PRIORITY_USER);

    g_free(css_path);
}

static void
css_load_default_css(GtkCssProvider *provider)
{
    gtk_css_provider_load_from_resource(provider, "io/github/trbjo/bob/launcher/Application.css");
}

static void
css_on_css_file_changed(GFileMonitor *monitor, GFile *file, GFile *other_file,
                         GFileMonitorEvent event_type, gpointer user_data)
{
    (void)monitor;
    (void)file;
    (void)other_file;
    (void)user_data;

    if (event_type == G_FILE_MONITOR_EVENT_CHANGED || event_type == G_FILE_MONITOR_EVENT_CREATED) {
        css_update_css_sheet();
        gtk_widget_queue_draw(GTK_WIDGET(bob_launcher_app_main_win));
        input_region_reset();
    }
}
