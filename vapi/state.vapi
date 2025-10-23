namespace BobLauncher {
    [CCode (cprefix = "bob_launcher_SEARCHING_FOR_", has_type_id = false)]
    public enum SearchingFor {
        CURRENT = -999,
        RESET = -1,
        PLUGINS = 0,
        SOURCES = 1,
        ACTIONS = 2,
        TARGETS = 3,
        COUNT = 4
    }

    [CCode (cheader_filename = "state.h", cname = "state_", lower_case_cprefix = "state_")]
    namespace State {
        [CCode (cname = "state_sf")]
        public static SearchingFor sf;

        [CCode (cname = "state_providers", array_length_cname = "state_providers_length1")]
        public static Hash.HashSet[] providers;

        [CCode (cname = "state_selected_indices", array_length_cname = "state_selected_indices_length1")]
        public static int[] selected_indices;

        [CCode (cname = "state_initialize")]
        public static void initialize();

        [CCode (cname = "state_current_provider")]
        public static unowned Hash.HashSet current_provider();

        [CCode (cname = "state_get_query")]
        public static unowned string get_query();

        [CCode (cname = "state_selected_plugin")]
        public static unowned Match? selected_plugin();

        [CCode (cname = "state_selected_source")]
        public static unowned Match? selected_source();

        [CCode (cname = "state_selected_action")]
        public static unowned Match? selected_action();

        [CCode (cname = "state_selected_target")]
        public static unowned Match? selected_target();

        [CCode (cname = "state_update_layout")]
        public static void update_layout(SearchingFor what = CURRENT);

        [CCode (cname = "state_reset")]
        public static void reset();
    }
}
