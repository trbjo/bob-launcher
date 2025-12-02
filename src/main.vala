namespace BobLauncher {
    public struct Score : int16 { }
    public const string BOB_LAUNCHER_APP_ID = "io.github.trbjo.bob.launcher";
    public const string BOB_LAUNCHER_OBJECT_PATH = "/io/github/trbjo/bob/launcher";

    namespace App {
        private const string socket_addr = "io.github.trbjo.bob.launcher";
        private const string socket_addr_sync = "io.github.trbjo.bob.launcher.sync";

        private const int SIGTERM = 15;
        private const int SIGINT = 2;

        internal static LauncherWindow? main_win = null;
        private static BobLaunchContext? launcher = null;
        private static MainLoop? main_loop = null;
        private static SocketService? socket_service = null;

        private static void open_uris(List<string> uris, string? hint) {
            string[] env = Environ.get();
            if (hint != null && hint != "") {
                env = Environ.set_variable(env, "XDG_ACTIVATION_TOKEN", hint);
            }

            launcher.launch_uris_internal(uris, env);
        }

        private static void quit() {
            if (main_loop != null) {
                main_loop.quit();
                main_loop = null;
            }
        }

        private static bool sigterm_signal_handler() {
            quit();
            return Source.REMOVE;
        }

        private static bool sigint_signal_handler() {
            quit();
            return Source.REMOVE;
        }

        private static void toggle_visibility() {
            main_win.set_visible(!main_win.visible);
        }

        private static void initialize(int listen_fd) {
            for (int i = 0; i < Threads.num_cores(); i++) {
                Threads.new_thread();
            }

            Gtk.init();
            Gtk.Settings.get_default().gtk_enable_accels = false;

            PluginLoader.initialize();
            State.initialize();
            IconCacheService.initialize();
            Keybindings.initialize();
            InputRegion.initialize();
            CSS.initialize();
            launcher = BobLaunchContext.get_instance();

            main_win = new LauncherWindow();
            main_win.close_request.connect(() => {
                quit();
                return false;
            });

            setup_socket_server(listen_fd);
            uint8[] socket_array = SystemdServiceUtils.make_abstract_socket_name(socket_addr_sync);
            SystemdServiceUtils.signal_ready(socket_array);
        }
        private static void select_plugin(LauncherWindow win, string plugin, string? query = null) {
            win.set_visible(Controller.select_plugin(plugin, query));
        }

        private static void setup_socket_server(int listen_fd) {
            socket_service = new SocketService();

            try {
                var socket = new Socket.from_fd(listen_fd);
                socket_service.add_socket(socket, null);

                socket_service.incoming.connect(on_incoming_connection);
                socket_service.start();

            } catch (Error e) {
                warning("[VALA] Failed to create socket server: %s", e.message);
            }
        }

        private static bool on_incoming_connection(SocketConnection connection) {
            try {
                var input = connection.input_stream;

                uint8 len_buf[4];
                size_t bytes_read;
                input.read_all(len_buf, out bytes_read);
                if (bytes_read != 4) {
                    warning("[VALA] Failed to read message length, got %zu bytes", bytes_read);
                    return false;
                }

                uint32 msg_len = ((uint32)len_buf[0]) |
                                ((uint32)len_buf[1] << 8) |
                                ((uint32)len_buf[2] << 16) |
                                ((uint32)len_buf[3] << 24);

                if (msg_len > 8192) {
                    warning("[VALA] Message too large: %u bytes", msg_len);
                    return false;
                }

                uint8[] msg_buf = new uint8[msg_len];
                input.read_all(msg_buf, out bytes_read);
                if (bytes_read != msg_len) {
                    warning("[VALA] Failed to read complete message, got %zu of %u bytes", bytes_read, msg_len);
                    return false;
                }

                // Extract token
                uint8 token_len = msg_buf[0];
                string? token = null;
                uint32 offset = 1;

                if (token_len > 0 && offset + token_len < msg_buf.length) {
                    token = (string)msg_buf[offset : offset + token_len + 1];  // +1 to include null terminator
                    Environment.set_variable("XDG_ACTIVATION_TOKEN", token, true);
                    offset += token_len + 1;  // +1 for null terminator
                }

                uint8 cmd = msg_buf[offset];
                offset++;

                switch (cmd) {
                    case 'A':  // Activate
                        toggle_visibility();
                        break;

                    case 'H':  // Hidden -- don't show
                        break;

                    case 'P':  // Plugin
                        if (offset + 2 > msg_len) {
                            warning("[VALA] Invalid plugin command");
                            break;
                        }

                        uint16 name_len = ((uint16)msg_buf[offset]) | ((uint16)msg_buf[offset + 1] << 8);
                        offset += 2;

                        if (offset + name_len > msg_len) {
                            warning("[VALA] Invalid plugin name length");
                            break;
                        }

                        string plugin_name = (string)msg_buf[offset : offset + name_len + 1];  // +1 for null
                        offset += name_len + 1;  // +1 for null terminator

                        string? query = null;
                        if (offset < msg_len) {
                            query = (string)msg_buf[offset : msg_len];
                            if (query == "") query = null;
                        }

                        select_plugin(main_win, plugin_name, query);
                        break;

                    case 'O':  // Open files
                        if (offset + 2 > msg_len) {
                            warning("[VALA] Invalid open command");
                            break;
                        }

                        uint16 count = ((uint16)msg_buf[offset]) | ((uint16)msg_buf[offset + 1] << 8);
                        offset += 2;

                        var uris = new List<string>();

                        for (int i = 0; i < count && offset < msg_len; i++) {
                            if (offset + 2 > msg_len) break;

                            uint16 len = ((uint16)msg_buf[offset]) | ((uint16)msg_buf[offset + 1] << 8);
                            offset += 2;

                            if (offset + len > msg_len) break;

                            string uri = (string)msg_buf[offset : offset + len + 1];  // +1 for null
                            if (FileUtils.test(uri, FileTest.EXISTS)) {
                                var f = File.new_for_path(uri);
                                uri = f.get_uri();
                            }

                            offset += len + 1;  // +1 for null terminator
                            uris.append(uri);
                        }

                        if (uris.length() > 0) {
                            open_uris(uris, token);
                        }
                        break;

                    default:
                        warning("[VALA] Unknown command: %c", cmd);
                        break;
                }
            } catch (Error e) {
                warning("[VALA] Error handling socket connection: %s", e.message);
            }

            return false; // Don't handle further connections on this thread
        }

        private static void shutdown() {
            SimpleKeyboard.teardown();
            PluginLoader.shutdown();

            if (main_win != null) {
                main_win.close();
                main_win.destroy();
                main_win = null;
            }

            launcher = null;

            if (socket_service != null) {
                socket_service.stop();
                socket_service = null;
            }

            Threads.join_all();
        }

        [CCode (cname = "run_launcher", has_target = false)]
        public static int run_launcher(int socket_fd) {
            main_loop = new MainLoop();

            GLib.Unix.signal_add(SIGINT, sigint_signal_handler);
            GLib.Unix.signal_add(SIGTERM, sigterm_signal_handler);

            initialize(socket_fd);

            main_loop.run();

            shutdown();

            return 0;
        }
    }
}
