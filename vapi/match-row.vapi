namespace BobLauncher {
    [CCode (cheader_filename = "match-row.h", cname = "BobLauncherMatchRow", type_id = "bob_launcher_match_row_get_type()")]
    public class MatchRow : Gtk.Widget {
        [CCode (cname = "abs_index")]
        public int abs_index;

        [CCode (cname = "event_id")]
        public int event_id;

        [CCode (cname = "bob_launcher_match_row_new", has_construct_function = false)]
        public MatchRow(int abs_index);

        [CCode (cname = "bob_launcher_match_row_update_match")]
        public void update_match(Levensteihn.StringInfo si);

        [CCode (cname = "bob_launcher_match_row_update")]
        public void update(Levensteihn.StringInfo si, int new_row, int new_abs_index, bool row_selected, int new_event);
    }
}
