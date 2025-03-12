namespace BobLauncher {
    namespace Utils {
        internal static Bytes load_file_content(string uri) {
            try {
                var file = File.new_for_uri(uri);
                uint8[] contents;
                string etag_out;

                if (file.load_contents(null, out contents, out etag_out)) {
                    return new Bytes(contents);
                } else {
                    warning("Failed to load file contents for URI: %s", uri);
                    return new Bytes(new uint8[0]);
                }
            } catch (Error e) {
                warning("Error loading file contents: %s", e.message);
                return new Bytes(new uint8[0]);
            }
        }

        public static string format_modification_time(DateTime now, DateTime mod_time) {
            var time_diff = now.difference(mod_time);

            if (time_diff < TimeSpan.MINUTE) {
                return "Just now";
            } else if (time_diff < TimeSpan.HOUR) {
                int minutes = (int)(time_diff / TimeSpan.MINUTE);
                return "%d minute%s ago".printf(minutes, minutes == 1 ? "" : "s");
            } else if (time_diff < TimeSpan.DAY) {
                int hours = (int)(time_diff / TimeSpan.HOUR);
                return "%d hour%s ago".printf(hours, hours == 1 ? "" : "s");
            } else if (time_diff < 7 * TimeSpan.DAY) {
                int days = (int)(time_diff / TimeSpan.DAY);
                return "%d day%s ago".printf(days, days == 1 ? "" : "s");
            } else {
                return mod_time.format("%Y-%m-%d");
            }
        }

        public static void open_command_line(string command, string? app_name = null, bool needs_terminal = false, string? working_dir = null) {
            AppInfoCreateFlags using_terminal = AppInfoCreateFlags.NONE;
            string? application_name = app_name;

            if (needs_terminal) {
                GLib.SettingsSchema schema = GLib.SettingsSchemaSource.get_default().lookup ("org.gnome.desktop.default-applications.terminal", true);

                if (schema != null) {
                    var settings = new GLib.Settings.full (schema, null, null);
                    application_name = settings.get_string ("exec");
                }

                if (application_name == null && Environment.find_program_in_path("x-terminal-emulator") != null) {
                    application_name = "x-terminal-emulator";
                }

                if (application_name == null) {
                    using_terminal = AppInfoCreateFlags.NEEDS_TERMINAL;
                    return;
                }
            }

            try {
                if (application_name == null) {
                    AppInfo app = AppInfo.create_from_commandline(command, application_name, using_terminal);
                    app.launch (null, Gdk.Display.get_default ().get_app_launch_context ());
                    return;
                }

                var builder = new StringBuilder(application_name);
                if (working_dir != null) {
                        builder.append_printf(" --working-directory='%s'", working_dir);
                }

                builder.append_printf (" -e %s", command);
                string commandline = builder.free_and_steal();

                debug(commandline);
                AppInfo app = AppInfo.create_from_commandline (commandline, application_name, using_terminal);
                app.launch (null, Gdk.Display.get_default ().get_app_launch_context ());
            }
            catch (Error err) {
                warning ("%s", err.message);
            }
        }
    }
}
