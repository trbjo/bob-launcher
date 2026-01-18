namespace BobLauncher {
    [CCode (cheader_filename = "launch-context.h", cname = "BobLauncherLaunchContext", type_id = "bob_launcher_bob_launch_context_get_type()")]
    public class BobLaunchContext : GLib.Object {
        [CCode (cname = "bob_launcher_bob_launch_context_get_instance")]
        public static unowned BobLaunchContext get_instance();

        [CCode (cname = "bob_launcher_bob_launch_context_launch_command")]
        public bool launch_command(string identifier, [CCode (array_length = false, array_null_terminated = true)] string[] argv, bool blocking = false, bool needs_terminal = false);

        [CCode (cname = "bob_launcher_bob_launch_context_launch_file")]
        public bool launch_file(GLib.File file);

        [CCode (cname = "bob_launcher_bob_launch_context_launch_uri")]
        public bool launch_uri(string uri);

        [CCode (cname = "bob_launcher_bob_launch_context_launch_app")]
        public bool launch_app(GLib.AppInfo app_info, bool needs_terminal = false, string? action = null);

        [CCode (cname = "bob_launcher_bob_launch_context_launch_with_uri")]
        public bool launch_with_uri(GLib.AppInfo app_info, string uri, string? action = null);

        [CCode (cname = "bob_launcher_bob_launch_context_launch_app_with_files")]
        public bool launch_app_with_files(GLib.AppInfo app_info, GLib.List<GLib.File> files, string? action = null);
    }
}
