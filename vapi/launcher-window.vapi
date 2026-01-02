namespace BobLauncher {
    [CCode (cheader_filename = "launcher-window.h", cname = "BobLauncherLauncherWindow", type_id = "bob_launcher_launcher_window_get_type()")]
    public class LauncherWindow : Gtk.Window {
        [CCode (cname = "bob_launcher_launcher_window_new", has_construct_function = false)]
        public LauncherWindow();

        [CCode (cname = "bob_launcher_launcher_window_ensure_surface")]
        public void ensure_surface();

        public bool client_side_shadow { get; set; }
        public bool inhibit_system_shortcuts { get; set; }
    }
}

[CCode (cheader_filename = "launcher-window.h")]
namespace InputRegion {
    [CCode (cname = "input_region_initialize")]
    public static void initialize();

    [CCode (cname = "input_region_reset")]
    public static void reset();
}
