[CCode (cprefix = "", lower_case_cprefix = "", cheader_filename = "unistd.h", cname = "execvpe")]
extern int execvpe (string path, [CCode (array_length = false, array_null_terminated = true)] string[] argv, [CCode (array_length = false, array_null_terminated = true)] string[] envp);

namespace BobLauncher {
    public class BobLaunchContext : Object {
        private static BobLaunchContext? instance = null;
        private ILaunchContext launcher;

        private BobLaunchContext() {
            bool is_systemd_available = Environment.get_variable("INVOCATION_ID") != null;

            if (is_systemd_available) {
                try {
                    var connection = Bus.get_sync(BusType.SESSION);
                    launcher = GLib.Object.new(typeof(SystemdLauncher), "connection", connection) as SystemdLauncher;
                    debug("Using SystemdLauncher");
                } catch (Error e) {
                    warning("Failed to initialize SystemdLauncher: %s", e.message);
                    launcher = new CommonLauncher();
                    debug("Falling back to CommonLauncher");
                }
            } else {
                launcher = new CommonLauncher();
                debug("Using CommonLauncher");
            }
        }

        public static unowned BobLaunchContext get_instance() {
            if (instance == null) {
                instance = new BobLaunchContext();
            }
            return instance;
        }

        public bool launch_command(string identifier, string[] argv, bool blocking = false, bool needs_terminal = false) {
            return launcher.launch_command(identifier, argv, null, blocking, needs_terminal);
        }

        public bool launch_file(File file) {

            var files = new List<File>();
            files.append(file);
            return launcher.launch_files(files);
        }

        internal bool launch_files_internal(List<File> files, string[] env) {
            return launcher.launch_files(files, env);
        }

        internal bool launch_uris_internal(List<string> uris, string[]? env = null) {
            return launcher.launch_uris(uris, env);
        }

        public bool launch_uri(string uri) {
            var uris = new List<string>();
            uris.append(uri);
            return launcher.launch_uris(uris);
        }

        public bool launch_app (GLib.AppInfo app_info, bool needs_terminal = false, string? action = null) {
            return launcher.launch_with_files(app_info, null, action);
        }

        public bool launch_with_uri(AppInfo app_info, string uri, string? action = null) {
            var uris = new List<string>();
            uris.append(uri);
            return launcher.launch_with_uris(app_info, uris, action);
        }

        public bool launch_app_with_files(AppInfo app_info, List<File> files, string? action = null) {
            return launcher.launch_with_files(app_info, files, action);
        }
    }
}
