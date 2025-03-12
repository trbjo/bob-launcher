namespace BobLauncher {
    private static void setup_click_controller(Gtk.Widget widget, AppSettings.LayerShell settings) {
        var click_controller = new Gtk.GestureClick();
        click_controller.set_button(0);
        widget.add_controller(click_controller);
        click_controller.pressed.connect((click_gesture, n_press, x, y) => {
            if (click_gesture.get_current_button() == Gdk.BUTTON_PRIMARY && n_press == 2) {
                bool is_exclusive = GtkLayerShell.get_keyboard_mode(App.main_win) == GtkLayerShell.KeyboardMode.EXCLUSIVE;
                GtkLayerShell.KeyboardMode new_mode = is_exclusive ? GtkLayerShell.KeyboardMode.ON_DEMAND : GtkLayerShell.KeyboardMode.EXCLUSIVE;
                settings.settings.set_enum("keyboard-mode", (int)new_mode);
                click_gesture.set_state(Gtk.EventSequenceState.CLAIMED);
                return;
            }

            if (n_press != 2) {
                return;
            }
            click_gesture.set_state(Gtk.EventSequenceState.CLAIMED);
            Gdk.Rectangle? rect = Utils.get_current_display_size(App.main_win);
            if (rect == null) return;

            int target_x = (int)(rect.width * settings.anchor_point_x - App.main_win.get_width() / 2);
            int target_y = (int)(rect.height * settings.anchor_point_y);

            GtkLayerShell.set_margin(App.main_win, GtkLayerShell.Edge.LEFT, target_x);
            Graphene.Rect sm_rect = Graphene.Rect();
            widget.compute_bounds(App.main_win, out sm_rect);
            int window_container_diff = (int)sm_rect.origin.y;
            GtkLayerShell.set_margin(App.main_win, GtkLayerShell.Edge.TOP, target_y - window_container_diff);
            GtkLayerShell.set_margin(App.main_win, GtkLayerShell.Edge.BOTTOM, 0);
            settings.change_points(settings.anchor_point_x, settings.anchor_point_y);
        });
    }
    internal delegate bool ShouldDrag(double x, double y);

    internal static void setup_drag_controller(Gtk.Widget widget, AppSettings.LayerShell settings, ShouldDrag should_drag) {
        int window_container_diff = 0;
        int original_offset_x = 0;
        int original_offset_y = 0;
        Graphene.Rect sm_rect = Graphene.Rect();

        Gtk.GestureDrag drag_gesture = new Gtk.GestureDrag();
        widget.add_controller(drag_gesture);
        drag_gesture.drag_begin.connect((x, y) => {
            if (!should_drag(x, y)) {
                drag_gesture.set_state(Gtk.EventSequenceState.DENIED);
                return;
            }

            widget.compute_bounds(App.main_win, out sm_rect);
            Gdk.Rectangle? rect = Utils.get_current_display_size(App.main_win);
            if (rect == null) return;
            Graphene.Point window_point = Graphene.Point();
            App.main_win.get_surface().set_cursor(new Gdk.Cursor.from_name("grab", null));

            bool window_point_computed = App.main_win.compute_point(App.main_win, { x: (float)x, y: (float)y }, out window_point);
            if (!window_point_computed) {
                return;
            }

            original_offset_y = rect.y;
            original_offset_x = rect.x;

            window_container_diff = (int)sm_rect.origin.y;
            drag_gesture.set_state(Gtk.EventSequenceState.CLAIMED);
            GtkLayerShell.set_anchor(App.main_win, GtkLayerShell.Edge.BOTTOM, false);
        });

        drag_gesture.drag_update.connect((x, y) => {
            Gdk.Rectangle? rect = Utils.get_current_display_size(App.main_win);
            if (rect == null) return;

            Graphene.Point window_point = Graphene.Point();
            bool window_point_computed = App.main_win.compute_point(App.main_win, { x: (float)x, y: (float)y }, out window_point);
            if (!window_point_computed) {
                return;
            }

            int horizontal_snap = (int)(rect.width * settings.anchor_point_x - sm_rect.size.width / 2 - sm_rect.origin.x) - original_offset_x + rect.x;
            int current_margin_x = GtkLayerShell.get_margin(App.main_win, GtkLayerShell.Edge.LEFT);
            double start_x = x - window_point.x + current_margin_x;
            int abs_x = (int)Math.round(start_x + x);

            if (Math.round(x) != 0) {

                // Calculate snap points
                int left_edge_snap = -(int)sm_rect.origin.x;
                int right_edge_snap = (int)(rect.width - sm_rect.size.width - sm_rect.origin.x);
                if (current_margin_x >= horizontal_snap && x < 0 && horizontal_snap - settings.anchor_snap_threshold < abs_x < horizontal_snap) {
                    abs_x = horizontal_snap;
                } else if (current_margin_x <= horizontal_snap && x > 0 && horizontal_snap < abs_x < horizontal_snap + settings.anchor_snap_threshold) {
                    abs_x = horizontal_snap;
                } else if (x < 0 && left_edge_snap - settings.anchor_snap_threshold < abs_x < left_edge_snap) {
                    abs_x = left_edge_snap;
                } else if (x > 0 && right_edge_snap < abs_x < right_edge_snap + settings.anchor_snap_threshold) {
                    abs_x = right_edge_snap;
                }

                if (current_margin_x != abs_x) {
                    GtkLayerShell.set_margin(App.main_win, GtkLayerShell.Edge.LEFT, abs_x);
                }
            }

            if (Math.round(y) != 0) {
                int current_margin_y = GtkLayerShell.get_margin(App.main_win, GtkLayerShell.Edge.TOP);
                int compare_pt = current_margin_y + window_container_diff;

                double start_y = y - window_point.y + compare_pt;
                int abs_y = (int)double.max(0, Math.floor(start_y + y));

                int vertical_snap = (int)(rect.height * settings.anchor_point_y) - original_offset_y + rect.y;
                int bottom_edge_snap = (int)(rect.height - sm_rect.size.height);
                bool check_y_center_snap = abs_x == horizontal_snap;

                if (check_y_center_snap && compare_pt >= vertical_snap && y < 0 && vertical_snap - settings.anchor_snap_threshold < abs_y && abs_y < vertical_snap) {
                    abs_y = vertical_snap;
                } else if (check_y_center_snap && compare_pt <= vertical_snap && y > 0 && vertical_snap < abs_y && abs_y < vertical_snap + settings.anchor_snap_threshold) {
                    abs_y = vertical_snap;
                } else if (y > 0 && bottom_edge_snap < abs_y) {
                    abs_y = bottom_edge_snap; // hard limit
                } else {
                    Graphene.Rect main_rect = Graphene.Rect();
                    App.main_win.child.compute_bounds(App.main_win, out main_rect);
                    int soft_snap = (int)(rect.height - main_rect.size.height);
                    if (y > 0 && soft_snap < abs_y && abs_y < soft_snap + settings.anchor_snap_threshold) {
                        abs_y = soft_snap; // soft_limit
                    }
                }

                if (compare_pt != abs_y) {
                    GtkLayerShell.set_margin(App.main_win, GtkLayerShell.Edge.TOP, abs_y - window_container_diff);
                    GtkLayerShell.set_margin(App.main_win, GtkLayerShell.Edge.BOTTOM, (int)rect.height - (abs_y - window_container_diff));
                }
            }
        });

        drag_gesture.drag_end.connect((x, y) => {
            GtkLayerShell.set_anchor(App.main_win, GtkLayerShell.Edge.BOTTOM, true);
            App.main_win.get_surface().set_cursor(new Gdk.Cursor.from_name("default", null));

            Gdk.Rectangle? rect = Utils.get_current_display_size(App.main_win);
            if (rect == null) return;

            double x_percentage, y_percentage;
            double container_width = widget.get_width();

            // Calculate the minimum and maximum allowed percentages
            double min_x_percentage = - ((container_width / 2) / rect.width);
            double max_x_percentage = 1 - min_x_percentage;
            double min_y_percentage = - (window_container_diff / (double)rect.height);
            double max_y_percentage = 1;

            int left_margin = GtkLayerShell.get_margin(App.main_win, GtkLayerShell.Edge.LEFT);
            double center_x = left_margin + App.main_win.get_width() / 2.0;
            x_percentage = center_x / rect.width;

            int top_margin = GtkLayerShell.get_margin(App.main_win, GtkLayerShell.Edge.TOP);
            y_percentage = ((double)top_margin+window_container_diff) / (double)rect.height;

            if (!(min_x_percentage < x_percentage && x_percentage < max_x_percentage)) {
                x_percentage = x_percentage - Math.floor(x_percentage);
            }
            if (!(min_y_percentage <= y_percentage && y_percentage <= max_y_percentage)) {
                y_percentage = y_percentage - Math.floor(y_percentage);
            }

            settings.change_points(x_percentage, y_percentage);
            drag_gesture.reset();
        });
    }
}
