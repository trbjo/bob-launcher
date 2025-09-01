namespace BobLauncher {
    internal class UpDownResizeHandle : Gtk.Widget {
        private static Settings ui_settings;
        private Gdk.Cursor pointer;
        Gtk.GestureDrag drag_gesture;

        static construct {
            ui_settings = new Settings(BOB_LAUNCHER_APP_ID + ".ui");
            set_css_name("handle");
        }

        construct {
            css_classes = {"vertical"};
            pointer = new Gdk.Cursor.from_name("ns-resize", null);
            drag_gesture = new Gtk.GestureDrag();
            add_controller(drag_gesture);
            drag_gesture.drag_update.connect(on_drag_update);
            state_flags_changed.connect(on_state_flags_changed);
        }

        internal void on_drag_update(Gtk.GestureDrag drag, double x, double y) {
            int height = ResultBox.row_pool[0].get_height();
            if (height == 0) return;
            int result = ((int)y) / height;
            if (result == 0) return;

            int current = ui_settings.get_int("box-size");
            int new_value = result + current;

            var range = ui_settings.settings_schema.get_key("box-size").get_range();
            Variant range_variant;
            range.get("(sv)", null, out range_variant);

            int min, max;
            range_variant.get("(ii)", out min, out max);
            new_value = int.max(min, int.min(max, new_value));

            if (new_value != current) {
                ui_settings.set_int("box-size", new_value);
            }
        }

        private void on_state_flags_changed(Gtk.StateFlags old_flags) {
            if (ResultBox.visible_size == 0) {
                drag_gesture.set_propagation_phase(Gtk.PropagationPhase.NONE);
                return;
            }
            drag_gesture.set_propagation_phase(Gtk.PropagationPhase.BUBBLE);
            var new_flags = get_state_flags();
            bool is_hover = (new_flags & Gtk.StateFlags.PRELIGHT) != 0;
            if (is_hover) {
                set_cursor(pointer);
            }
        }
    }

}
