namespace BobLauncher {
    [CCode (cheader_filename = "match-row-label.h", cname = "BobLauncherMatchRowLabel", type_id = "bob_launcher_match_row_label_get_type()")]
    public class MatchRowLabel : Gtk.Widget {
        [CCode (cname = "bob_launcher_match_row_label_new")]
        public MatchRowLabel(string[] css_classes);

        [CCode (cname = "bob_launcher_match_row_label_set_text")]
        public void set_text(string text, Pango.AttrList? attrs);

        [CCode (cname = "bob_launcher_match_row_label_set_description")]
        public void set_description(Description desc);

        [CCode (cname = "bob_launcher_match_row_label_reset")]
        public void reset();
    }
}
