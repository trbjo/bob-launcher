namespace BobLauncher {
    namespace LayerShell {
        private static bool wants_layer_shell(BobLauncher.AppSettings settings) {
            if (!settings.layershell.enabled) {
                return false;
            }

            if (!GtkLayerShell.is_supported()) {
                warning("GtkLayerShell is not supported on your system, set the top_margin to -1 to disable ");
                return false;
            }
            return true;
        }

        private static void setup_layer_shell(Gtk.Window win, BobLauncher.AppSettings settings) {
            if (!wants_layer_shell(settings)) return;

            if (!GtkLayerShell.is_supported()) {
                error("Layershell is not supported on your system, disable it in settings");
            }

            if (win.get_realized()) {
                error("win must be called before the window is realized");
            }

            GtkLayerShell.init_for_window(win);
            GtkLayerShell.set_namespace(win, BOB_LAUNCHER_APP_ID);
            GtkLayerShell.set_layer(win, GtkLayerShell.Layer.OVERLAY);
            GtkLayerShell.set_anchor(win, GtkLayerShell.Edge.TOP, true);
            GtkLayerShell.set_anchor(win, GtkLayerShell.Edge.BOTTOM, true);
            GtkLayerShell.set_anchor(win, GtkLayerShell.Edge.LEFT, true);
            GtkLayerShell.set_exclusive_zone(win, -1);

            GtkLayerShell.set_keyboard_mode(win, (GtkLayerShell.KeyboardMode)settings.layershell.settings.get_enum("keyboard-mode"));

            settings.layershell.settings.changed["keyboard-mode"].connect(() => {
                int kb_mode = settings.layershell.settings.get_enum("keyboard-mode");
                GtkLayerShell.set_keyboard_mode(win, (GtkLayerShell.KeyboardMode)kb_mode);
            });

        }

        private static void adjust_layershell_margins(Gtk.Window win, int width, int height) {
            if (!GtkLayerShell.is_layer_window(win)) return;

            Graphene.Point container_point = Graphene.Point();
            win.child.compute_point(win, { x: 0, y: 0 }, out container_point);
            int offset = (int)container_point.y;
            var settings = AppSettings.get_default();
            Gdk.Rectangle? rect = Utils.get_current_display_size(win);
            if (rect == null) return;

            // Calculate center position in pixels
            double _base_width = (double)width;
            double _rect_width = (double)rect.width;
            double center_x = (settings.layershell.point_x * _rect_width);
            int center_y = (int)(settings.layershell.point_y * rect.height);

            int left_margin = (int)Math.round(center_x - _base_width / 2.0);
            GtkLayerShell.set_margin(win, GtkLayerShell.Edge.LEFT, left_margin);
            GtkLayerShell.set_margin(win, GtkLayerShell.Edge.TOP, center_y - offset);
        }
    }
}
