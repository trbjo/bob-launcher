namespace BobLauncher {
    [CCode (cname = "MatchFactory", cheader_filename = "result-container.h", has_type_id = false, has_target = true, delegate_target = true, delegate_target_cname = "factory_user_data", has_type_id=false)]
    public delegate BobLauncher.Match MatchFactory();

    [Compact]
    [CCode (cheader_filename = "result-container.h", cname = "ResultContainer", free_function = "")]
    public class ResultContainer {
        public unowned Levensteihn.StringInfo string_info;
        public unowned Levensteihn.StringInfo string_info_spaceless;

        [CCode (cname = "result_container_get_query")]
        public unowned string get_query ();

        [CCode (cname = "result_container_add_lazy_unique")]
        public void add_lazy_unique (double relevancy, owned MatchFactory factory);

        [CCode (cname = "result_container_add_lazy")]
        public void add_lazy (uint hash, double relevancy, owned MatchFactory factory);

        [CCode (cname = "result_container_has_match")]
        public bool has_match (string? haystack);

        [CCode (cname = "result_container_match_score_with_offset")]
        public double match_score_with_offset (string? haystack, uint offset);

        [CCode (cname = "result_container_match_score")]
        public double match_score (string? haystack);

        [CCode (cname = "result_container_match_score_spaceless")]
        public double match_score_spaceless (string? haystack);

        [CCode (cname = "result_container_is_cancelled")]
        public bool is_cancelled ();
    }
}
