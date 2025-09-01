namespace CSS {
    private static GLib.Settings? settings;
    private static FileMonitor? file_monitor;
    private static Gtk.CssProvider? css_provider;
    private static Gtk.CssProvider? opacity_provider;

    private const string color_variables = """
@define-color base_transparent       alpha(@theme_base_color, %.2f);
@define-color bg_transparent         alpha(@theme_bg_color, %.2f);

@define-color separator_color             alpha(mix(@theme_base_color, #000, 0.23), %.2f);
@define-color unfocused_fg_color          mix(@theme_base_color, @theme_text_color, 0.5);
@define-color selected_match_row          alpha(mix(@theme_bg_color, @theme_text_color, 0.12), %.2f);
""";

    private const string color_variables_opaque = """
@define-color base_transparent       @theme_base_color;
@define-color bg_transparent         @theme_bg_color;

@define-color unfocused_fg_color          mix(@theme_base_color, @theme_text_color, 0.5);
@define-color separator_color             mix(@theme_base_color, #000, 0.23);
@define-color selected_match_row          mix(@theme_base_color, @theme_text_color, 0.12);

#result-box {
    background: @base_transparent;
}

""";

    internal static void initialize() {
        opacity_provider = new Gtk.CssProvider();
        settings = new GLib.Settings(BobLauncher.BOB_LAUNCHER_APP_ID + ".ui");

        set_css_opacity();
        settings.changed["opacity"].connect(set_css_opacity);

        update_css_sheet();
        settings.changed["css-sheet"].connect(update_css_sheet);
    }

    private static void set_css_opacity() {
        double opacity = settings.get_double("opacity");
        StyleProvider.remove_provider_for_display(Gdk.Display.get_default(), opacity_provider);
        // allows compositors to optimize by setting the color of the resultbox to an opaque color.
        string alpha_colors = opacity == 1.0 ? color_variables_opaque : color_variables.printf(opacity, opacity, opacity, opacity, opacity, opacity);
        opacity_provider.load_from_string(alpha_colors);
        StyleProvider.add_provider_for_display(Gdk.Display.get_default(), opacity_provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

    private void unload_css() {
        if (css_provider != null) {
            StyleProvider.remove_provider_for_display(Gdk.Display.get_default(), css_provider);
            css_provider = null;
        }
        css_provider = new Gtk.CssProvider();
    }

    private static void update_css_sheet() {
        if (file_monitor != null) {
            file_monitor.changed.disconnect(on_css_file_changed);
            file_monitor.cancel();
            file_monitor = null;
        }

        unload_css();

        string css_path = settings.get_string("css-sheet");

        if (FileUtils.test(css_path, FileTest.EXISTS)) {
            try {
                css_provider.load_from_path(css_path);

                // Set up a new file monitor
                var file = File.new_for_path(css_path);
                file_monitor = file.monitor_file(FileMonitorFlags.NONE);
                file_monitor.changed.connect(on_css_file_changed);
            } catch (Error e) {
                warning("Error loading CSS from path: %s", e.message);
                unload_css();
                load_default_css(css_provider);
            }
        } else {
            load_default_css(css_provider);
        }
        StyleProvider.add_provider_for_display(Gdk.Display.get_default(), css_provider, Gtk.STYLE_PROVIDER_PRIORITY_USER);
    }

    private void load_default_css(Gtk.CssProvider css_provider) {
        css_provider.load_from_resource("io/github/trbjo/bob/launcher/Application.css");
    }

    private void on_css_file_changed(File file, File? other_file, FileMonitorEvent event_type) {
        if (event_type == FileMonitorEvent.CHANGED || event_type == FileMonitorEvent.CREATED) {
            update_css_sheet();
            BobLauncher.App.main_win.queue_draw();
            InputRegion.reset();
        }
    }
}
