namespace BobLauncher {
    internal class LauncherWindow : Gtk.Window {
        private static int last_width;
        private static int last_height;
        private static UpDownResizeHandle? up_down_handle;
        private static WidthResizeHandle? width_handle;
        private static unowned AppSettings? appsettings;
        private static GLib.Settings? settings;

        construct {
            appsettings = AppSettings.get_default();

            settings = new GLib.Settings(BOB_LAUNCHER_APP_ID + ".ui");

            settings.changed["client-side-shadow"].connect(handle_shadow_settings);
            handle_shadow_settings();

            settings.changed["client-side-border"].connect(handle_border_settings);
            handle_border_settings();

            can_focus = false;
            focusable = false;
            deletable = false;
            resizable = false;
            handle_menubar_accel = false;

            title = "BobLauncher";
            name = "launcher";
            child = new MainContainer();

            up_down_handle = new UpDownResizeHandle();
            up_down_handle.set_parent(this);

            width_handle = new WidthResizeHandle();
            width_handle.set_parent(this);

            set_default_size(1, 1);

            LayerShell.setup_layer_shell(this, appsettings);
            map.connect(disable_controllers);
            map.connect_after(enable_custom_controller);
        }

        private void handle_border_settings() {
            if (settings.get_boolean("client-side-border")) {
                this.add_css_class("client-side-border");
            } else {
                this.remove_css_class("client-side-border");
            }
        }

        private void handle_shadow_settings() {
            if (settings.get_boolean("client-side-shadow")) {
                this.add_css_class("client-side-shadow");
            } else {
                this.remove_css_class("client-side-shadow");
            }
        }

        private void disable_controllers() {
            var controller_list = this.observe_controllers();
            for (int i = (int)controller_list.get_n_items() - 1; i >= 0; i--) {
                Gtk.EventController controller = (Gtk.EventController)controller_list.get_item((uint)i);
                controller.set_propagation_phase(Gtk.PropagationPhase.NONE);
                controller.reset();
                ((Gtk.Widget)this).remove_controller(controller);
            }
            map.disconnect(disable_controllers);
        }

        private void enable_custom_controller() {
            var native = this.get_native();
            var gdk_surface = native.get_surface();
            unowned var wayland_surface = ((Gdk.Wayland.Surface)gdk_surface).get_wl_surface();

            SimpleKeyboard.initialize(
                wayland_surface,
                Controller.handle_key_press,
                Controller.handle_key_release,
                Controller.handle_focus_enter,
                Controller.handle_focus_leave
            );

            map.disconnect(enable_custom_controller);
        }

        private const int handle_size = 4;
        Graphene.Rect inner;
        Gsk.Transform height_transform;
        Gsk.Transform width_transform;

        protected override void size_allocate(int base_width, int base_height, int baseline) {
            base.size_allocate(base_width, base_height, baseline);
            if (last_width != base_width || base_height != last_height) {
                if (!child.compute_bounds(this, out inner)) critical("could not calculate bounds");
                height_transform = new Gsk.Transform().translate({(int)inner.origin.x, (int)(inner.size.height + inner.origin.y) - handle_size / 2});
                width_transform = new Gsk.Transform().translate({(int)(inner.size.width + inner.origin.x) - handle_size / 2, (int)inner.origin.y});
                last_width = base_width;
                last_height = base_height;
                LayerShell.adjust_layershell_margins(App.main_win, base_width, base_height);
                InputRegion.set_input_regions(App.main_win, base_width, base_height);
            }
            up_down_handle.allocate((int)inner.size.width, handle_size, baseline, height_transform);
            width_handle.allocate(handle_size, (int)inner.size.height, baseline, width_transform);
        }

        protected override void snapshot(Gtk.Snapshot snapshot) {
            snapshot_child(child, snapshot);
        }


        internal override void hide() {
            ((Gdk.Wayland.Toplevel) this.get_surface()).restore_system_shortcuts();
            this.remove_css_class("completed");
            base.hide();
            State.reset();
            if (appsettings.layershell.enabled) GtkLayerShell.set_margin(this, GtkLayerShell.Edge.BOTTOM, 0);
        }

        public override void show() {
            base.show();
            var surf = ((Gdk.Wayland.Toplevel) get_surface());
            surf.inhibit_system_shortcuts(null);
            surf.set_application_id(BOB_LAUNCHER_APP_ID);
        }
    }
}
