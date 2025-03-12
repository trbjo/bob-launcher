namespace BobLauncher {
    namespace ScrollController {
        private static uint scroll_tick_id;
        private static double remaining_velocity = 0;
        private static double accumulated_scroll = 0.0;
        private static double initial_velocity = 0.0;
        private static double scroll_accumulator = 0.0;
        private static bool scrolling_down = true;
        private static double current_item_height;

        internal static void reset() {
            uint prev = Atomics.uexchange(ref scroll_tick_id, 0);
            if (prev != 0) App.main_win.remove_tick_callback(prev);
            scroll_accumulator = remaining_velocity = accumulated_scroll = 0;
        }

        private static Gtk.EventControllerScrollFlags flags = Gtk.EventControllerScrollFlags.VERTICAL | Gtk.EventControllerScrollFlags.KINETIC;

        internal static void setup(ResultBox result_box) {
            var scroll_controller = new Gtk.EventControllerScroll(flags);
            result_box.add_controller(scroll_controller);
            scroll_controller.decelerate.connect(handle_decelerate);
            scroll_controller.scroll.connect(handle_scroll);
        }

        private static bool handle_scroll(double dx, double dy) {
            bool local_direction = dy > 0;
            if (local_direction != scrolling_down) {
                scrolling_down = local_direction;
                uint prev = Atomics.uexchange(ref scroll_tick_id, 0);
                if (prev != 0) App.main_win.remove_tick_callback(prev);
                accumulated_scroll = remaining_velocity = initial_velocity = 0;
            }
            accumulated_scroll += dy;
            current_item_height = (double)ResultBox.row_pool[0].get_height();

            if (accumulated_scroll >= current_item_height) {
                Controller.goto_match(1);
                accumulated_scroll -= current_item_height;
            }

            if (accumulated_scroll <= -current_item_height) {
                Controller.goto_match(-1);
                accumulated_scroll += current_item_height;
            }
            State.update_layout();
            return true;
        }

        private static void handle_decelerate(double vel_x, double vel_y) {
            initial_velocity += Math.fabs(vel_y);
            remaining_velocity += Math.fabs(vel_y);
            if (scroll_tick_id == 0) scroll_tick_id = App.main_win.add_tick_callback(tick_callback);
        }

        private static bool tick_callback(Gtk.Widget widget, Gdk.FrameClock frame_clock) {
            double velocity_ratio = remaining_velocity / current_item_height;
            double items_to_scroll = Math.log10(velocity_ratio);

            if (items_to_scroll < 0.45) {
                accumulated_scroll = remaining_velocity = initial_velocity = scroll_tick_id = 0;
                return false;
            } else if (items_to_scroll > 1.0 || scroll_accumulator > 1.0) {
                int items = int.max(1, (int)Math.round(items_to_scroll));
                Controller.goto_match(scrolling_down ? items : -items);
                remaining_velocity -= current_item_height * items;
                scroll_accumulator = 0.0;
                State.update_layout();
            } else {
                scroll_accumulator += Math.pow(scroll_accumulator, 4.0) + Math.pow(items_to_scroll - Math.floor(items_to_scroll), 4.0);
            }
            return true;
        }
    }
}
