namespace BobLauncher {
    [CCode (cname = "MatchFactory", cheader_filename = "result-container.h", has_type_id = false, has_target = true, delegate_target = true, delegate_target_cname = "factory_user_data", has_type_id=false)]
    public delegate BobLauncher.Match MatchFactory();

    [Compact]
    [CCode (cheader_filename = "result-container.h", cname = "ResultContainer", free_function = "")]
    public class ResultContainer {
        [CCode (cname = "result_container_get_query")]
        public unowned string get_query ();

        [CCode (cname = "result_container_add_lazy_unique")]
        public void add_lazy_unique (int16 relevancy, owned MatchFactory factory);

        [CCode (cname = "result_container_add_lazy")]
        public void add_lazy (uint32 hash, int16 relevancy, owned MatchFactory factory);

        [CCode (cname = "result_container_has_match")]
        public bool has_match (string? haystack);

        [CCode (cname = "result_container_match_score")]
        public int16 match_score (string? haystack);

        [CCode (cname = "result_container_match_score_spaceless")]
        public int16 match_score_spaceless (string? haystack);

        [CCode (cname = "result_container_is_cancelled")]
        public bool is_cancelled ();
    }
}
