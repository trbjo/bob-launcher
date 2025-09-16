namespace Hash {
    [Compact]
    [CCode (cheader_filename = "hashset.h", cname = "HashSet", free_function="", ref_function="", unref_function = "", has_type_id = false)]
    public class HashSet {
        [CCode (cname = "hashset_create_default_handle")]
        public BobLauncher.ResultContainer create_handle();

        public unowned string query;
        public unowned Levensteihn.StringInfo string_info;
        public unowned Levensteihn.StringInfo string_info_spaceless;
        public int event_id;
        public int size;

        [CCode (cname = "hashset_merge")]
        public extern void merge(BobLauncher.ResultContainer rc);

        [CCode (cname = "hashset_get_match_at")]
        public extern unowned BobLauncher.Match? get_match_at(int index);

        [CCode (cname = "hashset_prepare")]
        public extern bool prepare();

        [CCode (cname = "hashset_destroy")]
        public extern void destroy();

        [CCode (cname = "hashset_create")]
        public HashSet(string query, int event_id);
    }
}
