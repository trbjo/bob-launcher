namespace BobLauncher {
    internal class WidthResizeHandle : Gtk.Widget {
        private static Gtk.CssProvider css_provider;
        private static Settings ui_settings;
        private Gdk.Cursor pointer;
        Gtk.GestureDrag drag_gesture;

        static construct {
            css_provider = new Gtk.CssProvider();
            ui_settings = new Settings(BOB_LAUNCHER_APP_ID + ".ui");
        }

        construct {
            pointer = new Gdk.Cursor.from_name("ew-resize", null);
            drag_gesture = new Gtk.GestureDrag();
            add_controller(drag_gesture);
            drag_gesture.drag_update.connect(on_drag_update);
            state_flags_changed.connect(on_state_flags_changed);
            css_set_width(ui_settings.get_int("width"));
            ui_settings.changed["width"].connect(() => css_set_width(ui_settings.get_int("width")));
        }

        internal void on_drag_update(double x, double y) {
            int current = ui_settings.get_int("width");
            int new_value = current + (int)x;

            var range = ui_settings.settings_schema.get_key("width").get_range();
            Variant range_variant;
            range.get("(sv)", null, out range_variant);

            int min, max;
            range_variant.get("(ii)", out min, out max);
            new_value = int.max(min, int.min(max, new_value));

            if (new_value != current) {
                ui_settings.set_int("width", new_value);
            }
        }

        internal void css_set_width(int width) {
            StyleProvider.remove_provider_for_display(Gdk.Display.get_default(), css_provider);
            css_provider.load_from_string("#main-container { min-width: %ipx; }".printf(width));
            StyleProvider.add_provider_for_display(Gdk.Display.get_default(), css_provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION);
        }

        private void on_state_flags_changed(Gtk.StateFlags old_flags) {
            drag_gesture.set_propagation_phase(Gtk.PropagationPhase.BUBBLE);
            var new_flags = get_state_flags();
            bool is_hover = (new_flags & Gtk.StateFlags.PRELIGHT) != 0;
            if (is_hover) {
                set_cursor(pointer);
            }
        }
    }
}
