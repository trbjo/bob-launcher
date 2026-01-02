namespace BobLauncher {
    [CCode (cheader_filename = "result-box.h", cname = "BobLauncherResultBox", type_id = "bob_launcher_result_box_get_type()")]
    public class ResultBox : Gtk.Widget {
        [CCode (cname = "bob_launcher_result_box_new")]
        public ResultBox();

        [CCode (cname = "bob_launcher_result_box_update_layout")]
        public void update_layout(Hash.HashSet provider, int selected_index);

        [CCode (cname = "bob_launcher_result_box_box_size")]
        public static int box_size;

        [CCode (cname = "bob_launcher_result_box_visible_size")]
        public static int visible_size;

        [CCode (cname = "bob_launcher_result_box_row_pool")]
        internal static MatchRow[] row_pool;
    }
}
