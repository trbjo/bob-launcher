namespace BobLauncher {
    private class ProgressIndicatorWidget : Gtk.Widget {
        construct {
            name = "progress-indicator";
        }
    }

    internal class MainContainer : Gtk.Widget {
        private static QueryContainer? qc;
        private static ProgressIndicatorWidget? progress_indicator;
        private static ResultBox? result_box;

        private static double fraction;
        private static Graphene.Rect? rect;

        private static int qc_height = 1;
        private static int box_height = 1;
        private static int bar_height = 1;

        construct {
            rect = Graphene.Rect();
            overflow = Gtk.Overflow.HIDDEN;
            name = "main-container";

            qc = new QueryContainer();
            qc.set_parent(this);

            progress_indicator = new ProgressIndicatorWidget();
            progress_indicator.set_parent(this);

            result_box = new ResultBox();
            result_box.set_parent(this);

            setup_click_controller();
        }

        internal override Gtk.SizeRequestMode get_request_mode() {
            return Gtk.SizeRequestMode.CONSTANT_SIZE;
        }

        protected override void measure(Gtk.Orientation orientation, int for_size, out int minimum, out int natural, out int minimum_baseline, out int natural_baseline) {
            minimum_baseline = natural_baseline = -1;
            minimum = natural = 0;

            if (orientation == Gtk.Orientation.VERTICAL) {
                qc.measure(Gtk.Orientation.VERTICAL, -1, null, out qc_height, null, null);
                progress_indicator.measure(Gtk.Orientation.VERTICAL, -1, null, out bar_height, null, null);
                result_box.measure(Gtk.Orientation.VERTICAL, -1, null, out box_height, null, null);
                minimum = natural = qc_height + box_height;
            } else {
                result_box.measure(Gtk.Orientation.HORIZONTAL, -1, null, null, null, null);
            }
        }

        protected override void size_allocate(int base_width, int base_height, int baseline) {
            qc.allocate(base_width, qc_height, baseline, null);
            Gsk.Transform transform = new Gsk.Transform().translate({ 0, qc_height });
            progress_indicator.allocate(base_width, bar_height, baseline, transform);
            result_box.allocate(base_width, box_height, baseline, transform);
        }

        protected override void snapshot(Gtk.Snapshot snapshot) {
            snapshot_child(qc, snapshot);
            snapshot_child(result_box, snapshot);

            var width = get_width();

            var progress_width = (int)(width * fraction);
            rect.init(0, qc_height, progress_width, bar_height);

            snapshot.push_clip(rect);
            unowned Gdk.RGBA accent_color = Highlight.get_accent_color();
            snapshot.append_color(accent_color, rect);

            snapshot_child(progress_indicator, snapshot);
            snapshot.pop();
        }

        internal static void update_layout(Hash.HashSet provider, int selected_index) {
            fraction = provider.size > 1 ? ((double)selected_index) / ((double)(provider.size - 1)) : 0.0;
            result_box.update_layout(provider, selected_index);
        }

        private void setup_click_controller() {
            Gtk.GestureClick click = new Gtk.GestureClick();
            click.set_button(0);
            result_box.add_controller(click);
            click.released.connect(handle_click_release);
        }

        private void handle_click_release(Gtk.GestureClick controller, int n_press, double x, double y) {
            unowned Gtk.Widget? picked_widget = result_box.pick(x, y, Gtk.PickFlags.DEFAULT);
            if (picked_widget == null) return;
            bool ctrl_pressed = false;
            var display = controller.get_device().get_display();
            var seat = display.get_default_seat();
            var keyboard = seat.get_keyboard();

            if (keyboard != null) {
                var modifier_state = keyboard.get_modifier_state();
                ctrl_pressed = (modifier_state & Gdk.ModifierType.CONTROL_MASK) != 0;
            }


            var frag = (Description)picked_widget.get_data<Description>("fragment");
            if (frag != null && frag.fragment_func != null) {
                try {
                    frag.fragment_func();
                    if (!ctrl_pressed) App.main_win.set_visible(false);
                } catch (Error e) {
                    warning("Failed to execute fragment action: %s", e.message);
                }
            } else {
                var item = picked_widget.get_ancestor(typeof(MatchRow));
                if (item != null) {
                    unowned MatchRow mr = (MatchRow)item;
                    Controller.goto_match_abs(mr.abs_index);
                    Controller.execute(!ctrl_pressed);
                }
            }
        }
    }
}
