namespace Hash {
    [Compact]
    [CCode (cheader_filename = "hashset.h", cname = "HashSet", free_function="", ref_function="", unref_function = "", has_type_id = false)]
    public class HashSet {
        public unowned string query;
        public unowned Levensteihn.StringInfo string_info;
        public unowned Levensteihn.StringInfo string_info_spaceless;
        public int event_id;
        public int size;

        [CCode (cname = "hashset_get_match_at")]
        public extern unowned BobLauncher.Match? get_match_at(int index);
    }
}
