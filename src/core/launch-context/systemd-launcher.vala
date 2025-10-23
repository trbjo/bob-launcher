[CCode (cheader_filename = "access-appinfo-props.h")]
namespace Props {
    [CCode (cname = "g_desktop_app_info_get_string_from_group", has_type_id = false)]
    public static extern string? desktop_app_info_get_string_from_group (GLib.DesktopAppInfo info, string group_name, string key);
}

namespace BobLauncher {
    internal class SystemdLauncher : Object, ILaunchContext {
        private const string LAUNCH_FILE_ATTRIBUTES = FileAttribute.STANDARD_TYPE + "," +
                                                      FileAttribute.STANDARD_CONTENT_TYPE;

        private string own_slice;
        public string systemd_slice { get; set; default = ""; }
        public string terminal_emulator { get; set; default = ""; }
        private GLib.Settings settings;
        public DBusConnection connection { get; construct; }
        private DBusScope.Monitor scope_monitor;
        private HashTable<string, string> dbus_name_to_object_path;
        private Spinlock.AtomicInt* token;

        construct {
            token = Spinlock.new();
            dbus_name_to_object_path = new HashTable<string, string>(str_hash, str_equal);

            int ret = SystemdScope.get_caller_systemd_slice(out own_slice);
            if (ret != 0) {
                error("Failed to get slice: %s", strerror(-ret));
            }

            settings = new GLib.Settings(BOB_LAUNCHER_APP_ID);
            settings.bind("systemd-slice", this, "systemd_slice", SettingsBindFlags.GET);
            settings.bind("terminal-emulator", this, "terminal_emulator", SettingsBindFlags.GET);
            scope_monitor = new DBusScope.Monitor(on_dbus_event);
            scope_monitor.start();
        }

        public void on_dbus_event(DBusScope.EventType event_type, string dbus_name, string? object_path) {
            Spinlock.spin_lock(token);
            if (event_type == DBusScope.EventType.CONNECTED) {
                dbus_name_to_object_path.set(dbus_name, object_path);
            } else if (event_type == DBusScope.EventType.DISCONNECTED) {
                dbus_name_to_object_path.remove(dbus_name);
            }
            Spinlock.spin_unlock(token);
        }

        public bool launch_files(List<File> files, string[]? env = null) {
            if (files == null || files.length() == 0) {
                return false;
            }

            var app_infos = new GLib.HashTable<string, HashTable<AppInfo, GenericArray<string>>>(str_hash, str_equal);

            foreach (var file in files) {
                try {
                    string original_uri = file.get_uri();

                    string clean_path = file.get_path();
                    if (clean_path != null) {
                        var regex = /^(.+?)(?::\d+(?::\d+)?)?$/;
                        MatchInfo match_info;
                        if (regex.match(clean_path, 0, out match_info)) {
                            clean_path = match_info.fetch(1);
                        }
                    }

                    var clean_file = File.new_for_path(clean_path);

                    var info = clean_file.query_info(LAUNCH_FILE_ATTRIBUTES, FileQueryInfoFlags.NONE);
                    AppInfo? app_info = AppInfo.get_default_for_type(info.get_content_type(), false) ??
                                    AppInfo.get_default_for_type("text/plain", false); // treat unknowns as text
                    // if we don't even have a handler for "text/plain" warn and exit
                    if (app_info == null) {
                        warning("no handlers detected for %s, cannot open", original_uri);
                        continue;
                    }

                    string name = app_info.get_name();
                    var inner = app_infos.get(name);
                    if (inner == null) {
                        inner = new HashTable<AppInfo, GenericArray<string>>(direct_hash, direct_equal);
                        var array = new GenericArray<string>();
                        array.add(original_uri);
                        inner.set(app_info, array);
                        app_infos.set(name, inner);
                    } else {
                        if (inner.size() != 1) error ("size must be 1");
                        inner.foreach((app_info, array) => {
                            array.add(original_uri);
                        });
                    }

                } catch (Error e) {
                    warning("Could not query file info: %s", e.message);
                }
            }

            app_infos.foreach((name, inner) => {
                if (inner.size() != 1) error ("size must be 1");
                inner.foreach((app_info, array) => {
                    var uri_list = new GLib.List<string>();
                    array.foreach((elem) => uri_list.append(elem));
                    launch_with_uris_internal(app_info, uri_list, null, env);
                });
            });

            return true;
        }


        public bool launch_uris(List<string> uris, string[]? env = null) {
            if (uris == null || uris.length() == 0) {
                return false;
            }

            var app_infos = new GLib.HashTable<string, HashTable<AppInfo, GenericArray<string>>>(str_hash, str_equal);

            foreach (string uri in uris) {
                try {
                    string? scheme = Uri.parse_scheme(uri);
                    if (scheme == null) {
                        int colon_pos = uri.index_of(":");
                        if (colon_pos > 0) {
                            scheme = uri.substring(0, colon_pos);
                        }
                    }

                    if (scheme == null) {
                        continue;
                    }

                    AppInfo? app_info = null;

                    if (scheme == "file") {
                        var file = File.new_for_uri(uri);

                        var info = file.query_info(LAUNCH_FILE_ATTRIBUTES, FileQueryInfoFlags.NONE);
                        app_info = AppInfo.get_default_for_type(info.get_content_type(), false) ??
                                        AppInfo.get_default_for_type("text/plain", false); // treat unknowns as text
                        // if we don't even have a handler for "text/plain" warn and exit
                    } else {
                        app_info = AppInfo.get_default_for_uri_scheme(scheme);
                    }

                    if (app_info == null) {
                        warning("no handlers detected for %s, cannot open", uri);
                        continue;
                    }

                    string name = app_info.get_name();
                    var inner = app_infos.get(name);
                    if (inner == null) {
                        inner = new HashTable<AppInfo, GenericArray<string>>(direct_hash, direct_equal);
                        var array = new GenericArray<string>();
                        array.add(uri);
                        inner.set(app_info, array);
                        app_infos.set(name, inner);
                    } else {
                        if (inner.size() != 1) error ("size must be 1");
                        inner.foreach((app_info, array) => {
                            array.add(uri);
                        });
                    }

                } catch (Error e) {
                    warning("Could not query file info: %s", e.message);
                }
            }

            app_infos.foreach((name, inner) => {
                if (inner.size() != 1) error ("size must be 1");
                inner.foreach((app_info, array) => {
                    var uri_list = new GLib.List<string>();
                    array.foreach((elem) => uri_list.append(elem));
                    launch_with_uris_internal(app_info, uri_list, null, env);
                });
            });

            return true;
        }

        public bool launch_with_files(AppInfo app_info, List<File>? files = null, string? action = null, string[]? env = null) {
            return launch_with_files_internal(app_info, files, action, env, false) == 0;
        }

        public int launch_with_files_blocking(AppInfo app_info, List<File>? files = null, string? action = null, string[]? env = null) {
            return launch_with_files_internal(app_info, files, action, env, true);
        }

        public bool launch_with_uris(AppInfo app_info, List<string>? uris = null, string? action = null, string[]? env = null) {
            return launch_with_uris_internal(app_info, uris, action, env, false) == 0;
        }

        public int launch_with_uris_blocking(AppInfo app_info, List<string>? uris = null, string? action = null, string[]? env = null) {
            return launch_with_uris_internal(app_info, uris, action, env, true);
        }


        private int launch_with_files_internal(AppInfo app_info, List<File>? files = null, string? action = null, string[]? env = null, bool blocking = true) {
            string[] argv = get_argv_for_app(app_info, action, files);
            if (argv.length == 0) {
                return -1;
            }

            string[] final_env = env ?? Environ.get();
            bool has_activation_token = false;

            foreach (string env_var in final_env) {
                if (env_var.has_prefix("XDG_ACTIVATION_TOKEN=")) {
                    has_activation_token = true;
                    break;
                }
            }

            if (!has_activation_token) {
                string? token = get_activation_token(app_info);
                if (token != null) {
                    string[] new_env = new string[final_env.length + 1];
                    for (int i = 0; i < final_env.length; i++) {
                        new_env[i] = final_env[i];
                    }
                    new_env[final_env.length] = "XDG_ACTIVATION_TOKEN=" + token;
                    final_env = new_env;
                }
            }

            return launch_wrapper_internal(app_info, argv, final_env, blocking);
        }

        private int launch_with_uris_internal(AppInfo app_info, List<string>? uris = null, string? action = null, string[]? env = null, bool blocking = false) {
            string[] argv = get_argv_for_app_with_uris(app_info, action, uris);
            if (argv.length == 0) {
                return -1;
            }

            return launch_wrapper_internal(app_info, argv, env, blocking);
        }

        private int launch_wrapper_internal(AppInfo app_info, string[] argv, string[]? env, bool blocking = false) {
            string? desktop_app_id = app_info.get_id();
            if (desktop_app_id == null) {
                error("app: %s, %s, does not have an id",
                                                  app_info.get_name(),
                                                  app_info.get_display_name());
            }
            string app_id = desktop_app_id.substring(0, desktop_app_id.length - 8);

            Spinlock.spin_lock(token);

            string? object_path = dbus_name_to_object_path.get(app_id);
            if (object_path == null || argv.length > 1) {
                Spinlock.spin_unlock(token);
                return launch_with_systemd_scope_internal(app_id, argv, env, blocking);
            }
            Spinlock.spin_unlock(token);

            bool success = try_activate(app_info, app_id, object_path, null);
            return  success ? 0 : -1;
        }

        private bool try_activate(AppInfo app_info, string app_id, string object_path, List<string>? uris = null) {
            var platform_data = new VariantBuilder(new VariantType("a{sv}"));

            string? activation_token = get_activation_token(app_info);
            if (activation_token != null) {
                platform_data.add("{sv}", "desktop-startup-id", new Variant.string(activation_token));
                platform_data.add("{sv}", "activation-token", new Variant.string(activation_token));
            }

            try {
                Variant? result = null;

                if (uris != null && uris.length() > 0) {
                    var uri_array = new VariantBuilder(new VariantType("as"));
                    foreach (string uri in uris) {
                        uri_array.add("s", uri);
                    }

                    result = connection.call_sync(
                        app_id,
                        object_path,
                        "org.freedesktop.Application",
                        "Open",
                        new Variant("(asa{sv})", uri_array, platform_data),
                        null,
                        DBusCallFlags.NONE,
                        1000,
                        null
                    );
                } else {
                    result = connection.call_sync(
                        app_id,
                        object_path,
                        "org.freedesktop.Application",
                        "Activate",
                        new Variant("(a{sv})", platform_data),
                        null,
                        DBusCallFlags.NONE,
                        1000,
                        null
                    );
                }

                if (result != null) {
                    return true;
                } else {
                    return false;
                }

            } catch (Error e) {
                warning("try_activate: FAILED - D-Bus call threw error: %s", e.message);
                return false;
            }
        }

        public bool launch_command(string app_name, string[] argv, string[]? env, bool blocking = false, bool needs_terminal = false) {
            GenericArray<string> args = needs_terminal ? get_terminal_argv() : new GenericArray<string>();
            for (int i = 0; i < argv.length; i++) {
                args.add(argv[i]);
            }
            return launch_with_systemd_scope_internal(app_name, args.data, env, blocking) == 0;
        }

        private int launch_with_systemd_scope_internal(string app_name, string[] argv, string[]? _env, bool blocking) {
            string[] env = _env ?? Environ.get();

            string[] argv_duplicated = new string[argv.length];
            for (int i = 0; i < argv.length; i++) {
                argv_duplicated[i] = argv[i].dup();
            }

            if (blocking) {
                var child_pid = Posix.fork();

                if (child_pid < 0) {
                    critical("Failed to fork child process");
                    return -1;
                } else if (child_pid == 0) {

                    Posix.setsid();

                    string? scope_name = create_scope_for_self(app_name);
                    if (scope_name == null) {
                        warning("Failed to create systemd scope");
                        Posix._exit(1);
                    }

                    for (int fd = 0; fd < 1024; fd++) {
                        Posix.close(fd);
                    }

                    int devnull = Posix.open("/dev/null", Posix.O_RDWR);
                    if (devnull != 0) {
                        Posix.dup2(devnull, 0);
                    }
                    Posix.dup2(devnull, 1);
                    Posix.dup2(devnull, 2);
                    if (devnull > 2) {
                        Posix.close(devnull);
                    }

                    execvpe(argv_duplicated[0], argv_duplicated, env);

                    Posix._exit(127);
                }

                int status;
                Posix.waitpid(child_pid, out status, 0);

                if (Process.if_exited(status)) {
                    int exit_code = Process.exit_status(status);
                    return exit_code;
                } else if (Process.if_signaled(status)) {
                    int signal = Process.term_sig(status);
                    return 128 + signal;
                } else {
                    return -1;
                }

            } else {
                // Non-blocking original implementation (double fork)
                var child_pid = Posix.fork();

                if (child_pid < 0) {
                    critical("Failed to fork child process");
                    return -1;
                } else if (child_pid == 0) {
                    var grandchild_pid = Posix.fork();

                    if (grandchild_pid < 0) {
                        Posix._exit(1);
                    } else if (grandchild_pid == 0) {
                        Posix.setsid();

                        string? scope_name = create_scope_for_self(app_name);
                        if (scope_name == null) {
                            warning("Failed to create systemd scope");
                            Posix._exit(1);
                        }

                        for (int fd = 0; fd < 1024; fd++) {
                            Posix.close(fd);
                        }

                        int devnull = Posix.open("/dev/null", Posix.O_RDWR);
                        if (devnull != 0) {
                            Posix.dup2(devnull, 0);
                        }
                        Posix.dup2(devnull, 1);
                        Posix.dup2(devnull, 2);
                        if (devnull > 2) {
                            Posix.close(devnull);
                        }

                        execvpe(argv_duplicated[0], argv_duplicated, env);

                        warning("Failed to exec %s: %s", argv_duplicated[0], strerror(errno));
                        Posix._exit(127);
                    } else {
                        Posix._exit(0);
                    }
                }

                int status;
                Posix.waitpid(child_pid, out status, 0);

                if (status != 0) {
                    warning("Child process exited with status %d", status);
                    return -1;
                }

                return 0;
            }
        }

        private string? create_scope_for_self(string app_name) {
            var escaped_name = new StringBuilder();

            foreach (char c in app_name.replace(" ", "").to_utf8()) {
                if (c.isalnum() || c == ':' || c == '_' || c == '.') {
                    escaped_name.append_c(c);
                } else {
                    escaped_name.append_printf("\\x%02x", (uint8)c);
                }
            }

            string scope_name = @"$(escaped_name.str)-$((int)Posix.getpid()).scope";

            string unit_slice = (systemd_slice.length > 6 && systemd_slice.has_suffix(".slice"))
                                ? systemd_slice : own_slice;

            string? job_path = SystemdScope.create_scope_lowlevel(unit_slice, scope_name, (Posix.pid_t)Posix.getpid());

            if (job_path != null) {
                return scope_name;
            } else {
                warning("Failed to create systemd scope");
                return null;
            }
        }

        private GenericArray<string> get_terminal_argv() {
            GenericArray<string> new_args = new GenericArray<string>();
            string[] terminal = terminal_emulator.split(" ");
            for (int i = 0; i < terminal.length; i++) {
                new_args.add(terminal[i]);
            }
            return new_args;
        }

        private string[] get_argv_for_app(AppInfo app_info, string? action, List<File>? files) {
            if (app_info is DesktopAppInfo) {
                var desktop_info = app_info as DesktopAppInfo;
                string? commandline = null;

                if (action != null) {
                    string action_group = "Desktop Action " + action;
                    commandline = Props.desktop_app_info_get_string_from_group(desktop_info, action_group, "Exec");
                } else {
                    commandline = desktop_info.get_commandline();
                }

                if (commandline != null) {
                    try {
                        string[] parts;
                        Shell.parse_argv(commandline, out parts);

                        bool needs_terminal = desktop_info.get_boolean("Terminal");
                        GenericArray<string> argv = needs_terminal ? get_terminal_argv() : new GenericArray<string>();

                        foreach (var part in parts) {
                            if (part == "%f" || part == "%F") {
                                if (files != null) {
                                    foreach (var file in files) {
                                        string? path = file.get_path();
                                        if (path != null) {
                                            argv.add(path);
                                        } else {
                                            // Fallback to URI if no path available
                                            argv.add(file.get_uri());
                                        }
                                    }
                                }
                            } else if (part == "%u" || part == "%U") {
                                if (files != null) {
                                    foreach (var file in files) {
                                        argv.add(file.get_uri());
                                    }
                                }
                            } else if (!part.contains("%")) {
                                // Regular argument without placeholder
                                argv.add(part);
                            }
                        }

                        return argv.data;
                    } catch (Error e) {
                        warning("Failed to parse command line: %s", e.message);
                        return {};
                    }
                }
            }

            warning("Cannot get commandline for non-DesktopAppInfo");
            return {};
        }

        private string[] get_argv_for_app_with_uris(AppInfo app_info, string? action, List<string>? uris) {
            if (app_info is DesktopAppInfo) {
                var desktop_info = app_info as DesktopAppInfo;
                string? commandline = null;

                if (action != null) {
                    string action_group = "Desktop Action " + action;
                    commandline = Props.desktop_app_info_get_string_from_group(desktop_info, action_group, "Exec");
                } else {
                    commandline = desktop_info.get_commandline();
                }

                if (commandline != null) {
                    try {
                        string[] parts;
                        Shell.parse_argv(commandline, out parts);

                        var argv = new GenericArray<string>();

                        bool needs_terminal = desktop_info.get_boolean("Terminal");

                        if (needs_terminal) {
                            string[] terminal = terminal_emulator.split(" ");
                            for (int i = 0; i < terminal.length; i++) {
                                argv.add(terminal[i]);
                            }
                        }

                        foreach (var part in parts) {
                            if (part == "%f" || part == "%F") {
                                // File placeholders - convert URI to file path
                                if (uris != null) {
                                    foreach (var uri in uris) {
                                        var file = File.new_for_uri(uri);
                                        var path = file.get_path();
                                        if (path != null) {
                                            argv.add(path);
                                        } else {
                                            // Fallback if get_path() returns null (e.g., for remote URIs)
                                            argv.add(uri);
                                        }
                                    }
                                }
                            } else if (part == "%u" || part == "%U") {
                                if (uris != null) {
                                    foreach (var uri in uris) {
                                        argv.add(uri);
                                    }
                                }
                            } else if (!part.contains("%")) {
                                argv.add(part);
                            }
                        }

                        return argv.data;
                    } catch (Error e) {
                        warning("Failed to parse command line: %s", e.message);
                        return {};
                    }
                }
            }

            warning("Cannot get commandline for non-DesktopAppInfo");
            return {};
        }

        private string? get_activation_token(AppInfo app_info) {
            var display = Gdk.Display.get_default();
            var args = new List<File>();
            if (display != null) {
                return display.get_app_launch_context()?.get_startup_notify_id(app_info, args);
            }
            return null;
        }
    }
}
