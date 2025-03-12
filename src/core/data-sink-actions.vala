namespace DataSink {
    internal static Hash.HashSet search_for_actions(string query, BobLauncher.Match m, int event_id) {
        var rs = new BobLauncher.ActionSet(m, query, event_id, MatchScore.THRESHOLD);

        foreach (var plg in PluginLoader.loaded_plugins) {
            if (plg.is_enabled()) {
                plg.find_for_match(m, rs);
            }
        }

        return rs.to_hashset();
    }
}

namespace BobLauncher {
    public class ActionSet {
        private Hash.HashSet hsh;
        public bool query_empty;
        public Match m;
        private Score score_threshold;
        private Levensteihn.StringInfo? si;
        private BobLauncher.ResultContainer rc;

        internal Hash.HashSet to_hashset() {
            hsh.complete_merge_handle(rc);
            return hsh;
        }

        public void add_action(Action action) {
            Score basic_relevancy = action.get_relevancy(m);
            if (basic_relevancy < score_threshold) {
                return;
            }

            MatchFactory make_match = () => {
                return action;
            };

            if (query_empty) {
                rc.add_lazy_unique(basic_relevancy, (owned)make_match);
            } else if (Levensteihn.query_has_match(si, action.get_title())) {
                basic_relevancy = Levensteihn.match_score(si, action.get_title());
                rc.add_lazy_unique(basic_relevancy, (owned)make_match);
            } else if (Levensteihn.query_has_match(si, action.get_description())) {
                basic_relevancy = Levensteihn.match_score(si, action.get_description());
                rc.add_lazy_unique(basic_relevancy, (owned)make_match);
            }
        }

        public ActionSet(Match? m, string query, int current_event, Score score_threshold) {
            this.m = m;
            this.query_empty = query.char_count() == 0;
            this.si = Levensteihn.StringInfo.create(query);
            this.score_threshold = score_threshold;
            this.hsh = new Hash.HashSet(query, current_event);
            this.rc = hsh.create_handle();
        }
    }

}
