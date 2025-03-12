namespace BobLauncher {
    namespace State {
        internal static SearchingFor sf;
        internal static Hash.HashSet[] providers;
        internal static int[] selected_indices;
        private static StringBuilder[] queries;
        private static int[] cursor_positions;

        internal static Hash.HashSet empty_provider(int event_id) {
            return new Hash.HashSet("", event_id);
        }

        internal static void initialize() {
            sf = SearchingFor.SOURCES;
            providers        = new Hash.HashSet[SearchingFor.COUNT];
            queries          = new StringBuilder[SearchingFor.COUNT];
            cursor_positions = new int[SearchingFor.COUNT];
            selected_indices = new int[SearchingFor.COUNT];

            for (int i = 0; i < SearchingFor.COUNT; i++) {
                selected_indices[i] = 0;
                cursor_positions[i] = 0;
                providers[i]        = empty_provider(0);
                queries[i]          = new StringBuilder();
            }
        }

        internal static bool is_in_initial_state() {
            return sf == SearchingFor.SOURCES && providers[SearchingFor.PLUGINS].get_match_at(selected_indices[SearchingFor.PLUGINS]) == null && get_query() == "";
        }

        internal static unowned Hash.HashSet current_provider() {
            return providers[sf];
        }

        internal unowned Match? selected_plugin() {
            return providers[SearchingFor.PLUGINS].get_match_at(selected_indices[SearchingFor.PLUGINS]);
        }

        internal unowned Match? selected_source() {
            return providers[SearchingFor.SOURCES].get_match_at(selected_indices[SearchingFor.SOURCES]);
        }

        internal unowned Match? selected_action() {
            return providers[SearchingFor.ACTIONS].get_match_at(selected_indices[SearchingFor.ACTIONS]);
        }

        internal unowned Match? selected_target() {
            return providers[SearchingFor.TARGETS].get_match_at(selected_indices[SearchingFor.TARGETS]);
        }


        internal static bool update_provider(SearchingFor what, Hash.HashSet? new_provider, int selected_index = 0) {
            if (new_provider.event_id < providers[sf].event_id) {
                Threads.run(new_provider.destroy);
                return false;
            }

            selected_indices[what] = int.max(0, int.min(new_provider.size - 1, selected_index));

            unowned Hash.HashSet old = providers[what];
            providers[what] = new_provider;
            Threads.run(old.destroy);
            return true;
        }

        internal static void update_layout(SearchingFor what = sf) {
            sf = what;
            MainContainer.update_layout(providers[sf], selected_indices[sf]);
        }

        internal static void reset() {
            int new_event_id = Events.increment();
            for (int i = SearchingFor.PLUGINS; i < SearchingFor.COUNT; i++) {
                update_provider(i, empty_provider(new_event_id));
                queries[i] = new StringBuilder();
                selected_indices[i] = 0;
                cursor_positions[i] = 0;
            }
            update_layout(SearchingFor.SOURCES);
            QueryContainer.adjust_label_for_query("", 0);
        }

        internal static void change_category(SearchingFor what) {
            if (sf == what) return;
            bool should_update = what < sf; // show cached matches
            int new_event_id = Events.increment();
            for (int i = what+1; i < SearchingFor.COUNT; i++) {
                update_provider(i, empty_provider(new_event_id));
                queries[i] = new StringBuilder();
            }
            sf = what == SearchingFor.RESET ? SearchingFor.SOURCES : what;

            string text = queries[sf].str;
            Controller.start_search(text);
            change_cursor_position(text.length);
            QueryContainer.adjust_label_for_query(text, cursor_positions[sf]);
            if (should_update) update_layout();
        }

        internal static unowned string get_query() {
            return queries[sf].str;
        }

        internal static void delete_line() {
            if (get_query() != "") {
                queries[sf] = new StringBuilder();
                Controller.start_search(queries[sf].str);
                cursor_positions[sf] = 0;
                QueryContainer.adjust_label_for_query("", 0);
            }
        }

        internal static void append_query(string? tail) {
            if (tail == null) return;
            queries[sf].insert(cursor_positions[sf], tail);
            cursor_positions[sf]+=tail.length;

            Controller.start_search(queries[sf].str);
            QueryContainer.adjust_label_for_query(get_query(), cursor_positions[sf]);
        }

        private static bool change_cursor_position(int index) {
            int tmp = index.clamp(0, get_query().length);
            if (tmp == cursor_positions[sf]) return false;
            cursor_positions[sf] = tmp;
            return true;
        }


        internal static void char_left() {
            if (change_cursor_position(cursor_positions[sf] - 1)) {
                QueryContainer.adjust_label_for_query(get_query(), cursor_positions[sf]);
            }
        }

        internal static void char_right() {
            if (change_cursor_position(cursor_positions[sf] + 1)) {
                QueryContainer.adjust_label_for_query(get_query(), cursor_positions[sf]);
            }
        }

        internal static void word_left() {
            int next_word = find_next_word(false);
            if (change_cursor_position(next_word)) {
                QueryContainer.adjust_label_for_query(get_query(), cursor_positions[sf]);
            }
        }

        internal static void word_right() {
            int next_word = find_next_word(true);
            if (change_cursor_position(next_word)) {
                QueryContainer.adjust_label_for_query(get_query(), cursor_positions[sf]);
            }
        }

        internal static void line_begin() {
            if (change_cursor_position(0)) {
                QueryContainer.adjust_label_for_query(get_query(), cursor_positions[sf]);
            }
        }

        internal static void line_end() {
            if (change_cursor_position(get_query().length)) {
                QueryContainer.adjust_label_for_query(get_query(), cursor_positions[sf]);
            }
        }

        internal static void delete_char_backward() {
            if (queries[sf].len == 0) return;
            if (!change_cursor_position(cursor_positions[sf] - 1)) return;
            queries[sf].erase(cursor_positions[sf], 1);

            Controller.start_search(queries[sf].str);
            QueryContainer.adjust_label_for_query(get_query(), cursor_positions[sf]);
        }

        internal static void delete_char_forward() {
            if (queries[sf].len == cursor_positions[sf]) return;
            queries[sf].erase(cursor_positions[sf], 1);

            Controller.start_search(queries[sf].str);
            QueryContainer.adjust_label_for_query(get_query(), cursor_positions[sf]);
        }

        internal static void delete_word() {
            if (cursor_positions[sf] == 0) return;

            int next_word = find_next_word(false);
            if (next_word == cursor_positions[sf]) return;

            int length = cursor_positions[sf] - next_word;
            queries[sf].erase(next_word, length);
            change_cursor_position(next_word);

            Controller.start_search(queries[sf].str);
            QueryContainer.adjust_label_for_query(get_query(), cursor_positions[sf]);
        }

        private static int find_next_word(bool right) {
            unowned StringBuilder builder = queries[sf];
            int index = cursor_positions[sf];
            unowned string text = builder.str;
            unichar c = text.get_char(index);
            if (right) {
                while (text.get_next_char(ref index, out c) && StringUtils.is_word_boundary(c)) {}
                while (true) {
                    if (!text.get_next_char(ref index, out c)) {
                        return index;
                    }
                    if (StringUtils.is_word_boundary(c)) {
                        return index-1;
                    }
                }
            } else {
                while (text.get_prev_char(ref index, out c) && StringUtils.is_word_boundary(c)) {}
                while (true) {
                    if (!text.get_prev_char(ref index, out c)) {
                        return index;
                    }
                    if (StringUtils.is_word_boundary(c)) {
                        return index+1;
                    }
                }
            }
        }
    }
}
