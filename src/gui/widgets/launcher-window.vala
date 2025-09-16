namespace BobLauncher {
    internal class LauncherWindow : Gtk.Window {
        private static int last_width;
        private static int last_height;
        public static UpDownResizeHandle? up_down_handle;
        public static WidthResizeHandle? width_handle;
        private static unowned AppSettings? appsettings;
        private static GLib.Settings? settings;

        public bool client_side_shadow { get; set; }
        public bool inhibit_system_shortcuts { get; set; }

        private Gdk.Wayland.Toplevel surf;

        construct {
            appsettings = AppSettings.get_default();

            settings = new GLib.Settings(BOB_LAUNCHER_APP_ID + ".ui");

            settings.bind("client-side-shadow", this, "client_side_shadow", GLib.SettingsBindFlags.GET);
            settings.changed["client-side-shadow"].connect_after(handle_shadow_settings);
            handle_shadow_settings();

            var gtk_settings = Gtk.Settings.get_default();
            settings.bind("prefer-dark-theme", gtk_settings, "gtk-application-prefer-dark-theme", GLib.SettingsBindFlags.DEFAULT);

            settings.bind("inhibit-system-shortcuts", this, "inhibit_system_shortcuts", GLib.SettingsBindFlags.GET);
            settings.changed["inhibit-system-shortcuts"].connect_after(handle_shortcut_inhibit);

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
            disable_controllers();
        }

        private void handle_border_settings() {
            if (settings.get_boolean("client-side-border")) {
                this.add_css_class("client-side-border");
            } else {
                this.remove_css_class("client-side-border");
            }
        }

        private void handle_shortcut_inhibit() {
            if (inhibit_system_shortcuts && visible) {
                surf.inhibit_system_shortcuts(null);
            } else {
                surf.restore_system_shortcuts();
            }
        }

        private void handle_shadow_settings() {
            if (client_side_shadow) {
                this.add_css_class("client-side-shadow");
            } else {
                this.remove_css_class("client-side-shadow");
            }
        }

        private void disable_controllers() {
            var controller_list = this.observe_controllers();
            for (int i = (int)controller_list.get_n_items() - 1; i >= 0; i--) {
                Gtk.EventController? controller = controller_list.get_item((uint)i) as Gtk.EventController;
                if (controller != null) {
                    ((Gtk.Widget)this).remove_controller(controller);
                }
            }
        }

        private void on_monitor_changed() {
            Idle.add(() => {
                LayerShell.adjust_layershell_margins(this, last_width, last_height);
                return false;
            }, GLib.Priority.LOW);
        }

        Graphene.Rect inner;
        Gsk.Transform height_transform;
        Gsk.Transform width_transform;

        protected override void size_allocate(int base_width, int base_height, int baseline) {
            base.size_allocate(base_width, base_height, baseline);

            if (!child.compute_bounds(this, out inner)) critical("could not calculate bounds");
            int width_handle_width, up_down_handle_height;
            width_handle.measure(Gtk.Orientation.HORIZONTAL, -1, null, out width_handle_width, null, null);
            up_down_handle.measure(Gtk.Orientation.VERTICAL, -1, null, out up_down_handle_height, null, null);
            height_transform = new Gsk.Transform().translate({(int)inner.origin.x, (int)(inner.size.height + inner.origin.y) - up_down_handle_height});
            width_transform = new Gsk.Transform().translate({(int)(inner.size.width + inner.origin.x) - width_handle_width, (int)inner.origin.y});

            up_down_handle.allocate((int)inner.size.width, up_down_handle_height, baseline, height_transform);
            width_handle.allocate(width_handle_width, (int)inner.size.height, baseline, width_transform);
            if (last_width != base_width || base_height != last_height) {
                last_width = base_width;
                last_height = base_height;
                LayerShell.adjust_layershell_margins(this, base_width, base_height);
                if (client_side_shadow) InputRegion.set_input_regions(this, base_width, base_height);
            }
        }

        internal override void hide() {
            surf.restore_system_shortcuts();
            base.hide();
            State.reset();
        }

        internal void ensure_surface() {
            var mysurf = this.get_surface() as Gdk.Surface;
            mysurf.enter_monitor.connect_after(on_monitor_changed);

            var native = this.get_native();
            var gdk_surface = native.get_surface();
            surf = this.get_surface() as Gdk.Wayland.Toplevel;
            unowned var wayland_surface = ((Gdk.Wayland.Surface)gdk_surface).get_wl_surface();

            SimpleKeyboard.initialize(
                wayland_surface,
                Controller.handle_key_press,
                Controller.handle_key_release,
                Controller.handle_focus_enter,
                Controller.handle_focus_leave
            );
        }

        public override void show() {
            if (!this.get_realized()) {
                base.show();
                ensure_surface();
                if (inhibit_system_shortcuts) surf.inhibit_system_shortcuts(null);
                surf.set_application_id(BOB_LAUNCHER_APP_ID);
            } else {
                if (inhibit_system_shortcuts) surf.inhibit_system_shortcuts(null);
                surf.set_application_id(BOB_LAUNCHER_APP_ID);
                base.show();
            }
        }
    }
}
