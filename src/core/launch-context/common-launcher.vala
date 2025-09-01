namespace BobLauncher {
    internal class CommonLauncher : Object, ILaunchContext {
        private const string LAUNCH_FILE_ATTRIBUTES = FileAttribute.STANDARD_TYPE + "," +
                                                       FileAttribute.STANDARD_CONTENT_TYPE;

        public bool launch_files(List<File> files, string[]? env = null) {
            if (files == null || files.length() == 0) {
                return false;
            }

            var file = files.nth_data(0);

            try {
                var info = file.query_info(LAUNCH_FILE_ATTRIBUTES, FileQueryInfoFlags.NONE);
                var file_type = info.get_file_type();

                if (file_type == FileType.DIRECTORY) {
                    var uris = new List<string>();
                    foreach (var f in files) {
                        uris.append(f.get_uri());
                    }
                    return launch_uris(uris, env);
                } else {
                    var app_info = AppInfo.get_default_for_type(info.get_content_type(), false);
                    if (app_info != null) {
                        return launch_with_files(app_info, files, null, env);
                    }
                }
            } catch (Error e) {
                warning("Could not query file info: %s", e.message);
            }

            return false;
        }

        public bool launch_uris(List<string> uris, string[]? env = null) {
            if (uris == null || uris.length() == 0) {
                return false;
            }

            try {
                var context = get_launch_context();

                bool all_success = true;
                foreach (var uri in uris) {
                    if (!AppInfo.launch_default_for_uri(uri, context)) {
                        all_success = false;
                    }
                }

                return all_success;
            } catch (Error e) {
                warning("Could not launch URIs: %s", e.message);
                return false;
            }
        }

        public bool launch_command(string identifier, string[] argv, string[]? env = null, bool blocking = false, bool needs_terminal = false) {
            string command = string.joinv(" ", argv);
            Utils.open_command_line(command, identifier, needs_terminal, null);
            return true;
        }


        public bool launch_with_files(AppInfo app_info, List<File>? files = null, string? action = null, string[]? env = null) {
            try {
                var context = get_launch_context();
                return app_info.launch(files, context);
            } catch (Error e) {
                warning("Could not launch files with handler: %s", e.message);
                return false;
            }
        }

        public bool launch_with_uris(AppInfo app_info, List<string>? uris = null, string? action = null, string[]? env = null) {
            try {
                var context = get_launch_context();
                return app_info.launch_uris(uris, context);
            } catch (Error e) {
                warning("Could not launch URIs with handler: %s", e.message);
                return false;
            }
        }

        private static AppLaunchContext? get_launch_context() {
            var display = Gdk.Display.get_default();
            if (display != null) {
                return display.get_app_launch_context();
            }
            return null;
        }
    }
}
