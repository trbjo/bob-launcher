namespace BobLauncher {
    [CCode (cprefix = "bob_launcher_SEARCHING_FOR_", has_type_id = false)]
    public enum SearchingFor {
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

        [CCode (cname = "state_empty_provider")]
        public static Hash.HashSet empty_provider(int event_id);

        [CCode (cname = "state_initialize")]
        public static void initialize();

        [CCode (cname = "state_is_in_initial_state")]
        public static bool is_in_initial_state();

        [CCode (cname = "state_current_provider")]
        public static unowned Hash.HashSet current_provider();

        [CCode (cname = "state_selected_plugin")]
        public static unowned Match? selected_plugin();

        [CCode (cname = "state_selected_source")]
        public static unowned Match? selected_source();

        [CCode (cname = "state_selected_action")]
        public static unowned Match? selected_action();

        [CCode (cname = "state_selected_target")]
        public static unowned Match? selected_target();

        [CCode (cname = "state_update_provider")]
        public static bool update_provider(SearchingFor what, Hash.HashSet? new_provider, int selected_index = 0);

        [CCode (cname = "state_update_layout")]
        public static void update_layout(SearchingFor what = (SearchingFor)(-999));

        [CCode (cname = "state_reset")]
        public static void reset();

        [CCode (cname = "state_change_category")]
        public static void change_category(SearchingFor what);

        [CCode (cname = "state_get_query")]
        public static unowned string get_query();

        [CCode (cname = "state_delete_line")]
        public static void delete_line();

        [CCode (cname = "state_append_query")]
        public static void append_query(string? tail);

        [CCode (cname = "state_change_cursor_position")]
        public static bool change_cursor_position(int index);

        [CCode (cname = "state_char_left")]
        public static void char_left();

        [CCode (cname = "state_char_right")]
        public static void char_right();

        [CCode (cname = "state_word_left")]
        public static void word_left();

        [CCode (cname = "state_word_right")]
        public static void word_right();

        [CCode (cname = "state_line_begin")]
        public static void line_begin();

        [CCode (cname = "state_line_end")]
        public static void line_end();

        [CCode (cname = "state_delete_char_backward")]
        public static void delete_char_backward();

        [CCode (cname = "state_delete_char_forward")]
        public static void delete_char_forward();

        [CCode (cname = "state_delete_word")]
        public static void delete_word();

        [CCode (cname = "state_find_next_word")]
        public static int find_next_word(bool right);

        [CCode (cname = "state_cleanup")]
        public static void cleanup();
    }
}
