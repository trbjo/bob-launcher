namespace BobLauncher {
    [DBus(name = "io.github.trbjo.bob.launcher")]
    internal class App : Gtk.Application {
        internal static LauncherWindow? main_win = null;
        private static SettingsWindow? settings_window = null;

        construct {
            application_id = BOB_LAUNCHER_APP_ID;
            flags = ApplicationFlags.HANDLES_OPEN | ApplicationFlags.DEFAULT_FLAGS;
        }

        internal override void open(File[] files, string hint) {
            string[] env = Environ.get();
            if (hint != null && hint != "") {
                env = Environ.set_variable(env, "XDG_ACTIVATION_TOKEN", hint);
            }

            foreach (File f in files) {
                Utils.launch_file_raw(f, env);
            }
        }

        [DBus(name = "OpenSettings")]
        internal void open_settings() throws Error {
            if (settings_window != null) {
                settings_window.close();
            }
            settings_window = null;
            settings_window = new SettingsWindow(this, BOB_LAUNCHER_APP_ID, "BobLauncher");
            settings_window.present();
        }

        internal override void shutdown() {
            SimpleKeyboard.teardown();
            PluginLoader.shutdown();

            if (settings_window != null) {
                settings_window.close();
                settings_window = null;
            }

            if (main_win != null) {
                main_win.close();
                main_win = null;
            }

            Threads.join_all();
            base.shutdown();
        }

        private const int SIGTERM = 15;
        private const int SIGINT = 2;

        private bool sigterm_signal_handler() {
            message("Got SIGTERM");
            quit();
            return Source.REMOVE;
        }

        private bool sigint_signal_handler() {
            message("Got SIGINT");
            quit();
            return Source.REMOVE;
        }

        internal override void activate() {
            main_win.set_visible(!main_win.visible);
        }

        internal override void startup() {
            GLib.Unix.signal_add(SIGINT, sigint_signal_handler);
            GLib.Unix.signal_add(SIGTERM, sigterm_signal_handler);

            base.startup();
            Gtk.Settings.get_default().gtk_enable_accels = false;

            State.initialize();
            PluginLoader.initialize();
            IconCacheService.initialize();
            Keybindings.initialize();
            InputRegion.initialize();
            CSS.initialize();

            main_win = new LauncherWindow();

            main_win.close_request.connect(() => {
                this.release();
                return false;
            });

            this.hold();

            int cpus = Threads.num_cores();
            while (cpus-- > 0) Threads.new_thread();
        }

        [DBus(name = "SelectPlugin")]
        public void select_plugin(string plugin, string? query = null) throws Error {
            App.main_win.set_visible(Controller.select_plugin(plugin, query));
        }

        internal override bool dbus_register(DBusConnection connection, string object_path) throws Error {
            try {
                connection.register_object(object_path, this);
            } catch (IOError e) {
                error("Could not register object: %s", e.message);
            }
            return true;
        }
    }

    [CCode (cname = "run_launcher", has_target = false)]
    public static int run_launcher(int argc, char** argv) {
        var args = new string[argc];
        for (int i = 0; i < argc; i++) {
            args[i] = (string)argv[i];
        }
        return new App().run(args);
    }
}
