namespace BobLauncher {
    namespace BobAppInfo {
        public static string get_string_from_group(GLib.DesktopAppInfo info, string group_name, string key) {
            return Props.desktop_app_info_get_string_from_group(info, group_name, key);
        }
    }

    namespace Threading { // api for the plugins
        public delegate void TaskFunc();
        public static void run(owned TaskFunc task) {
            Threads.run((owned)task);
        }

        [CCode (cheader_filename = "immintrin.h", cname = "_mm_pause", has_type_id=false)]
        public static extern void pause();

        [CCode (cheader_filename = "stdatomic.h", cname = "atomic_load", has_type_id=false)]
        public static extern int atomic_load(ref int ptr);

        [CCode (cheader_filename = "stdatomic.h", cname = "atomic_exchange", has_type_id=false)]
        public static extern int atomic_exchange(ref int ptr, int value);

        [CCode (cheader_filename = "stdatomic.h", cname = "atomic_compare_exchange_strong", has_type_id=false)]
        public static extern bool cas(ref int ptr, ref int expected, int value);

        public static int atomic_inc(ref int ptr) {
            return Atomics.inc(ref ptr);
        }

        public static int atomic_dec(ref int ptr) {
            return Atomics.dec(ref ptr);
        }

        public static void atomic_store(ref int ptr, int value) {
            Atomics.store(ref ptr, value);
        }
    }

    namespace Strings {
        public static bool is_word_boundary(unichar c) {
            return StringUtils.is_word_boundary(c);
        }

        public static string? replace(string? str, string? chars, string? replacement) {
            return StringUtils.replace(str, chars, replacement);
        }

        public string? decode_html_chars(string? input) {
            return StringUtils.decode_html_chars(input);
        }
    }

    namespace Utils {
        internal delegate void ChildIterator(Gtk.Widget widget);

        public static string get_thumbnail_path(string file_path, int size) {
            var file = File.new_for_path(file_path);
            var uri = file.get_uri();
            var md5 = GLib.Checksum.compute_for_string(GLib.ChecksumType.MD5, uri);

            // Round up to nearest standard size
            string size_dir;
            if (size <= 128) size_dir = "normal";
            else if (size <= 256) size_dir = "large";
            else if (size <= 512) size_dir = "x-large";
            else size_dir = "xx-large";

            return Path.build_filename(
                Environment.get_user_cache_dir(),
                "thumbnails",
                size_dir,
                md5 + ".png"
            );
        }


        internal static void iterate_children(Gtk.Widget? child_widget, ChildIterator func) {
            while (child_widget != null) {
                unowned Gtk.Widget? sibling = child_widget.get_next_sibling();
                func(child_widget);
                child_widget = sibling;
            }
        }

        internal delegate T ChildArgIterator<T>(Gtk.Widget widget, T current_value);

        internal static Gdk.Rectangle? get_current_display_size(Gtk.Window window) {
            var mon = get_current_monitor(window);
            if (mon == null) {
                return null;
            }
            return mon.get_geometry();
        }

        internal static Gdk.Monitor? get_current_monitor(Gtk.Window window) {
            unowned Gdk.Surface? surface = window.get_surface();
            if (surface == null) {
                return null;
            }
            Gdk.Display? display = surface.get_display();
            if (display == null) {
                return null;
            }
            var monitor = display.get_monitor_at_surface(surface);
            if (monitor != null) {
                return monitor;
            }
            message("default monitor not found");

            unowned GLib.ListModel monitor_list = display.get_monitors();
            uint n_monitors = monitor_list.get_n_items();

            if (n_monitors == 0) {
                message("No monitors found");
                return null;
            }
            return (monitor_list.get_item(0) as Gdk.Monitor);
        }

        internal const string LAUNCH_FILE_ATTRIBUTES =
            FileAttribute.STANDARD_TYPE + "," +
            FileAttribute.STANDARD_CONTENT_TYPE + "," +
            FileAttribute.STANDARD_NAME + "," +
            FileAttribute.STANDARD_DISPLAY_NAME;

        public static bool launch_file(File file) {
            bool retval = false;
            try {

                var info = file.query_info(LAUNCH_FILE_ATTRIBUTES, FileQueryInfoFlags.NONE);
                var file_type = info.get_file_type();

                if (file_type == FileType.DIRECTORY) {
                    try {
                        AppInfo.launch_default_for_uri(file.get_uri(), null);
                        retval = true;
                    } catch (Error e) {
                        warning("Could not open folder: %s", e.message);
                    }
                } else {
                    try {
                        var app_info = AppInfo.get_default_for_type(info.get_content_type(), false);
                        if (app_info != null) {
                            var files = new List<File>();
                            files.append(file);
                            app_info.launch(files, null);
                            retval = true;
                        }
                    } catch (Error e) {
                        warning("Could not open file: %s", e.message);
                    }
                }
            } catch (Error e) {
                warning("Could not query file info: %s", e.message);
            }
            return retval;
        }

        public static void launch_app(AppInfo app_info) {
            try {
                app_info.launch(null, Gdk.Display.get_default().get_app_launch_context());
            } catch (Error err) {
                warning ("%s", err.message);
            }
        }

        public static bool is_all_lowercase(string str) {
            for (int i = 0; i < str.length; i++) {
                unichar c = str.get_char(i);
                if (c.isalpha() && !c.islower()) {
                    return false;
                }
            }
            return true;
        }

        internal static AppInfo? get_app_info_for_file(File file) {
            try {
                string path = file.get_path();

                FileInfo info = file.query_info(FileAttribute.STANDARD_CONTENT_TYPE,
                                              FileQueryInfoFlags.NONE);
                string content_type = info.get_content_type();

                AppInfo app_info = AppInfo.get_default_for_type(content_type, false);

                if (app_info == null) {
                    if (content_type == "application/x-executable" ||
                        content_type == "application/x-shellscript") {

                        FileInfo exec_info = file.query_info(FileAttribute.ACCESS_CAN_EXECUTE,
                                                          FileQueryInfoFlags.NONE);
                        if (exec_info.get_attribute_boolean(FileAttribute.ACCESS_CAN_EXECUTE)) {
                            message("File is executable, creating custom AppInfo");
                            AppInfo custom_app = AppInfo.create_from_commandline(
                                path, null, AppInfoCreateFlags.NONE);
                            return custom_app;
                        }
                    }

                    // Fallback to the MIME type based on the filename
                    string mime_type;
                    bool uncertain;
                    mime_type = ContentType.guess(path, null, out uncertain);
                    message("Fallback to extension-based MIME type: %s (uncertain: %s)",
                          mime_type, uncertain.to_string());

                    if (mime_type != content_type) {
                        app_info = AppInfo.get_default_for_type(mime_type, false);
                        if (app_info != null) {
                            return app_info;
                        }
                    }

                    return null;
                }

                return app_info;
            } catch (Error e) {
                message("Error determining application: %s", e.message);
            }

            string path = file.get_path();
            string mime_type;
            bool uncertain;
            mime_type = ContentType.guess(path, null, out uncertain);
            message("Error fallback MIME type: %s", mime_type);

            return AppInfo.get_default_for_type(mime_type, false);
        }

        internal static string[]? parse_command_line(string commandline, string file_path) {
            string[] argv;
            try {
                Shell.parse_argv(commandline, out argv);
            } catch (Error e) {
                message("Error parsing command: %s", e.message);
                return null;
            }

            bool replaced = replace_placeholders(ref argv, file_path);

            if (!replaced) {
                argv += file_path;
            }

            return argv;
        }

        internal static bool replace_placeholders(ref string[] argv, string path) {
            for (int i = 0; i < argv.length; i++) {
                if (argv[i] == "%f" || argv[i] == "%u" || argv[i] == "%F" || argv[i] == "%U") {
                    argv[i] = path;
                    return true;
                }
            }
            return false;
        }
    }
}

