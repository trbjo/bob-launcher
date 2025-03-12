namespace BobLauncher {
    internal class AppSettings {
        private static AppSettings? instance;
        internal UI ui { get; private set; }
        internal Plugins plugins { get; private set; }
        internal LayerShell layershell { get; private set; }

        private AppSettings() {
            instance = this;
            var main_settings = new GLib.Settings(BOB_LAUNCHER_APP_ID);
            ui = new UI(main_settings.get_child("ui"));
            layershell = new LayerShell(main_settings.get_child("layershell"));
            plugins = new Plugins(main_settings.get_child("plugins"));
        }

        internal static unowned AppSettings get_default() {
            if (instance == null) {
                instance = new AppSettings();
            }
            return instance;
        }

        internal class LayerShell {
            internal GLib.Settings settings;

            private double _anchor_snap_threshold;
            internal double point_x { get; private set; }
            internal double point_y { get; private set; }
            internal bool enabled { get; private set; }

            internal double anchor_snap_threshold { get { return _anchor_snap_threshold; } }
            internal double anchor_point_y { get; private set; }
            internal double anchor_point_x { get; private set; }

            internal LayerShell(GLib.Settings enabled_settings) {
                this.settings = enabled_settings.get_child("layershell");
                enabled = enabled_settings.get_boolean("enabled");
                update_all();
                this.settings.changed.connect(layershell_changed);
            }

            protected void update_all() {
                update_points();
                update_anchor_points();
                update_anchor_snap_threshold();
            }

            private void layershell_changed(string key) {
                switch (key) {
                    case "points":
                        update_points();
                        break;
                    case "anchor-points":
                        update_anchor_points();
                        break;
                    case "anchor-snap-threshold":
                        update_anchor_snap_threshold();
                        break;
                }
            }

            protected void update_anchor_snap_threshold() {
                _anchor_snap_threshold = (double)settings.get_int("anchor-snap-threshold");
            }

            protected void update_points() {
                Variant variant = settings.get_value("points");
                if (variant.is_of_type(new VariantType("(dd)"))) {
                    double x, y;
                    variant.get("(dd)", out x, out y);
                    change_points(x, y);
                }
            }

            protected void update_anchor_points() {
                Variant variant = settings.get_value("anchor-points");
                if (variant.is_of_type(new VariantType("(ii)"))) {
                    int x, y;
                    variant.get("(ii)", out x, out y);
                    anchor_point_x = ((double)x) / 100.0;
                    anchor_point_y = ((double)y) / 100.0;
                }
            }

            internal signal void point_changed(double x, double y);

            internal void change_points(double x, double y) {
                bool should_signal = false;
                if (x != point_x) {
                    should_signal = true;
                    point_x = x;
                }

                if (y != point_y) {
                    should_signal = true;
                    point_y = y;
                }

                if (should_signal) {
                    settings.set_value("points", new Variant("(dd)", x, y));
                    point_changed(point_x, point_y);
                }
            }
        }

        internal class UI {
            internal GLib.Settings settings;
            private string _shortcut_indicator;
            internal signal void accent_color_changed();

            internal UI(GLib.Settings settings) {
                this.settings = settings;
                update_all();
                this.settings.changed.connect(ui_changed);
            }

            private void ui_changed(string key) {
                switch (key) {
                    case "hide-after-dnd-success":
                        update_hide_after_dnd_success();
                        break;
                    case "accent-color":
                        update_accent_color();
                        break;
                    case "shortcut-indicator":
                        update_shortcut_indicator();
                        break;
                    case "css-sheet":
                        break;
                }
            }

            internal string shortcut_indicator { get { return _shortcut_indicator; } }

            protected void update_hide_after_dnd_success() {
               hide_after_dnd_success = settings.get_boolean("hide-after-dnd-success");
            }

            internal bool hide_after_dnd_success { get; private set; }

            protected void update_all() {
                update_accent_color();
                update_shortcut_indicator();
                update_hide_after_dnd_success();
            }

            protected void update_accent_color() {
                string color = settings.get_string("accent-color");
                Highlight.set_accent_color(color);
                accent_color_changed();
            }

            protected void update_shortcut_indicator() {
                _shortcut_indicator = settings.get_string("shortcut-indicator");
            }
        }

        internal class Plugins {
            internal GLib.Settings settings { get; private set; }
            internal GLib.HashTable<string, GLib.Settings> plugins { get; private set; }

            internal Plugins(GLib.Settings settings) {
                this.settings = settings;
                plugins = new GLib.HashTable<string, GLib.Settings>(str_hash, str_equal);
                load_plugins_from_schema();
            }

            private void load_plugins_from_schema() {
                foreach (var plugin_name in settings.list_children()) {
                    plugins[plugin_name] = settings.get_child(plugin_name);
                }
            }
        }
    }
}
