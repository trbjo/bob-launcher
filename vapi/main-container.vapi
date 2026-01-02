namespace BobLauncher {
    [CCode (cheader_filename = "main-container.h", cname = "BobLauncherMainContainer", type_id = "bob_launcher_main_container_get_type()")]
    public class MainContainer : Gtk.Widget {
        [CCode (cname = "bob_launcher_main_container_new", has_construct_function = false)]
        public MainContainer();

        [CCode (cname = "bob_launcher_main_container_update_layout")]
        public static void update_layout(Hash.HashSet provider, int selected_index);
    }
}
