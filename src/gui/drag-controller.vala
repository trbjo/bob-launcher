namespace BobLauncher {
    private static Gtk.GestureDrag drag_gesture;
    private static Gtk.GestureClick click_controller;

    internal delegate bool ShouldDrag(double x, double y);

    private int window_container_diff;

    private int monitor_start_x;
    private int monitor_start_y;

    private int monitor_width;
    private int monitor_height;

    private int prev_margin_x;
    private int prev_margin_y;

    private int drag_pt_within_container_x;
    private int drag_pt_within_container_y;

    private int container_width;
    private int container_height;

    private Graphene.Rect sm_rect;
    private unowned AppSettings.LayerShell settings;
    private ShouldDrag should_drag;

    private double last_x;
    private double last_y;

    private static void setup_click_controller(Gtk.Widget widget, AppSettings.LayerShell settings) {
        click_controller = new Gtk.GestureClick();
        click_controller.set_button(0);
        widget.add_controller(click_controller);
        click_controller.pressed.connect((click_gesture, n_press, x, y) => {
            if (click_gesture.get_current_button() == Gdk.BUTTON_PRIMARY && n_press == 2) {
                bool is_exclusive = GtkLayerShell.get_keyboard_mode(App.main_win) == GtkLayerShell.KeyboardMode.EXCLUSIVE;
                GtkLayerShell.KeyboardMode new_mode = is_exclusive ? GtkLayerShell.KeyboardMode.ON_DEMAND : GtkLayerShell.KeyboardMode.EXCLUSIVE;
                settings.settings.set_enum("keyboard-mode", (int)new_mode);
                drag_gesture.set_state(Gtk.EventSequenceState.DENIED);
                click_gesture.set_state(Gtk.EventSequenceState.CLAIMED);
                return;
            }

            if (n_press != 2) {
                return;
            }

            click_gesture.set_state(Gtk.EventSequenceState.CLAIMED);
            drag_gesture.set_state(Gtk.EventSequenceState.DENIED);
            Gdk.Rectangle? rect = Utils.get_current_display_size(App.main_win);
            if (rect == null) return;

            int target_x = (int)(rect.width * settings.anchor_point_x - App.main_win.get_width() / 2);
            int target_y = (int)(rect.height * settings.anchor_point_y);

            GtkLayerShell.set_margin(App.main_win, GtkLayerShell.Edge.LEFT, target_x);
            Graphene.Rect sm_rect = Graphene.Rect();
            widget.compute_bounds(App.main_win, out sm_rect);
            int window_container_diff = (int)sm_rect.origin.y;
            GtkLayerShell.set_margin(App.main_win, GtkLayerShell.Edge.TOP, target_y - window_container_diff);
            settings.change_points(settings.anchor_point_x, settings.anchor_point_y);
        });
    }

    internal static void setup_drag_controller(QueryContainer query_container, AppSettings.LayerShell layershell_settings, ShouldDrag drag_func) {
        sm_rect = Graphene.Rect();
        settings = layershell_settings;
        should_drag = drag_func;

        drag_gesture = new Gtk.GestureDrag();
        query_container.add_controller(drag_gesture);

        drag_gesture.drag_begin.connect(on_drag_begin);
        drag_gesture.drag_update.connect(on_drag_update);
        drag_gesture.drag_end.connect(on_drag_end);
    }

    private static void on_drag_begin(Gtk.GestureDrag drag_gesture, double x, double y) {
        if (!should_drag(x, y)) {
            drag_gesture.set_state(Gtk.EventSequenceState.DENIED);
            return;
        }
        click_controller.set_state(Gtk.EventSequenceState.DENIED);

        Graphene.Point window_point = Graphene.Point();
        if (!App.main_win.compute_point(App.main_win, { x: (float)x, y: (float)y }, out window_point)) {
            return;
        }

        Gdk.Rectangle?  monitor_dimensions = Utils.get_current_display_size(App.main_win);
        if (monitor_dimensions == null) {
            return;
        }

        drag_gesture.get_widget().compute_bounds(App.main_win, out sm_rect);
        window_container_diff = (int)sm_rect.origin.y;

        Graphene.Rect main_rect = Graphene.Rect();
        App.main_win.child.compute_bounds(App.main_win, out main_rect);
        container_height = (int)main_rect.size.height;
        container_width = (int)main_rect.size.width;


        monitor_start_y = monitor_dimensions.y;
        monitor_start_x = monitor_dimensions.x;
        monitor_width   = monitor_dimensions.width;
        monitor_height  = monitor_dimensions.height;

        prev_margin_x = GtkLayerShell.get_margin(App.main_win, GtkLayerShell.Edge.LEFT);
        prev_margin_y = GtkLayerShell.get_margin(App.main_win, GtkLayerShell.Edge.TOP);

        last_x = prev_margin_x;
        last_y = prev_margin_y + window_container_diff;

        drag_pt_within_container_x = (int)x;
        drag_pt_within_container_y = (int)y;

        drag_gesture.set_state(Gtk.EventSequenceState.CLAIMED);
        App.main_win.get_surface().set_cursor(new Gdk.Cursor.from_name("grab", null));
    }

    private static void on_drag_update(Gtk.GestureDrag drag_gesture, double x, double y) {

        int abs_x = (int)Math.round(prev_margin_x + x);
        int h_center = (int)(monitor_width * settings.anchor_point_x - sm_rect.size.width / 2 - sm_rect.origin.x) ;
        bool rightward = abs_x > last_x;

        int left_edge_snap = -(int)sm_rect.origin.x;
        int right_edge_snap = (int)(monitor_width - sm_rect.size.width - sm_rect.origin.x);

        if (abs_x >= h_center && abs_x <= h_center + settings.anchor_snap_threshold && rightward) {
            abs_x = h_center;
        } else if (abs_x <= h_center && abs_x >= h_center - settings.anchor_snap_threshold && !rightward) {
            abs_x = h_center;
        } else if (!rightward && left_edge_snap - settings.anchor_snap_threshold < abs_x && abs_x < left_edge_snap) {
            abs_x = left_edge_snap;
        } else if (rightward && right_edge_snap < abs_x && abs_x < right_edge_snap + settings.anchor_snap_threshold) {
            abs_x = right_edge_snap;
        }

        if (prev_margin_x != abs_x) {
            GtkLayerShell.set_margin(App.main_win, GtkLayerShell.Edge.LEFT, abs_x);
            last_x = abs_x;
        }

        bool check_y_center_snap = abs_x == h_center;
        int compare_pt = prev_margin_y + window_container_diff;
        int abs_y = (int)double.max(0, Math.floor(compare_pt + y));

        bool downward = abs_y > last_y;

        int v_center = (int)(monitor_height * settings.anchor_point_y);
        int bottom_edge_snap = (int)(monitor_height - sm_rect.size.height);

        if (check_y_center_snap && downward && abs_y >= v_center && abs_y <= v_center + settings.anchor_snap_threshold) {
            abs_y = v_center;
        } else if (check_y_center_snap && !downward && abs_y <= v_center && abs_y >= v_center - settings.anchor_snap_threshold) {
            abs_y = v_center;
        } else if (y > 0 && bottom_edge_snap < abs_y) {
            abs_y = bottom_edge_snap; // hard limit
        } else {
            int soft_snap = (int)(monitor_height - container_height);
            if (downward && soft_snap < abs_y && abs_y < soft_snap + settings.anchor_snap_threshold) {
                abs_y = soft_snap; // soft_limit
            }
        }

        if (prev_margin_y != abs_y - window_container_diff) {
            GtkLayerShell.set_margin(App.main_win, GtkLayerShell.Edge.TOP, abs_y - window_container_diff);
            last_y = abs_y;
        }
    }

    private static void on_drag_end(Gtk.GestureDrag drag_gesture, double x, double y) {
        drag_gesture.reset();
        App.main_win.get_surface().set_cursor(new Gdk.Cursor.from_name("default", null));

        double x_percentage, y_percentage;

        // Calculate the minimum and maximum allowed percentages
        double min_x_percentage = - ((container_width / 2) / monitor_width);
        double max_x_percentage = 1 - min_x_percentage;
        double min_y_percentage = - (window_container_diff / (double)monitor_height);
        double max_y_percentage = 1;

        int left_margin = GtkLayerShell.get_margin(App.main_win, GtkLayerShell.Edge.LEFT);
        double center_x = left_margin + App.main_win.get_width() / 2.0;
        x_percentage = center_x / monitor_width;

        int top_margin = GtkLayerShell.get_margin(App.main_win, GtkLayerShell.Edge.TOP);
        y_percentage = ((double)top_margin+window_container_diff) / (double)monitor_height;

        if (!(min_x_percentage < x_percentage && x_percentage < max_x_percentage)) {
            x_percentage = x_percentage - Math.floor(x_percentage);
        }
        if (!(min_y_percentage <= y_percentage && y_percentage <= max_y_percentage)) {
            y_percentage = y_percentage - Math.floor(y_percentage);
        }

        settings.change_points(x_percentage, y_percentage);
    }
}
