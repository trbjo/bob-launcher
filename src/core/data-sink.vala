namespace DataSink {
    internal static int find_plugin_by_name(string query) {
        int i = 0;
        foreach (BobLauncher.PluginBase plg in PluginLoader.search_providers) {
            if (plg.is_enabled()) {
                if (query == plg.to_string()) {
                    return i;
                }
                i++;
            }
        }
        return -1;
    }

    internal static Hash.HashSet search_for_plugins(string query, int event_id) {
        var hsh = new Hash.HashSet(query, event_id); // Single worker since not parallel
        BobLauncher.ResultContainer rc = hsh.create_handle();
        var si = Levensteihn.StringInfo.create(query);

        foreach (BobLauncher.PluginBase plg in PluginLoader.search_providers) {
            if (!plg.is_enabled() || !Levensteihn.query_has_match(si, plg.get_title())) {
                continue;
            }

            BobLauncher.MatchFactory make_match = () => plg;

            double score = Levensteihn.match_score(si, plg.get_title());
            rc.add_lazy_unique(score, (owned)make_match);
        }

        hsh.complete_merge_handle(rc);
        return hsh;
    }

    internal static Hash.HashSet search_for_targets(string query, BobLauncher.Match a, int event_id) {
        var hsh = new Hash.HashSet(query, event_id);
        BobLauncher.ResultContainer rc = hsh.create_handle();
        BobLauncher.Match m = new BobLauncher.UnknownMatch(query);
        BobLauncher.MatchFactory make_match = () => m;
        rc.add_lazy_unique(100, (owned)make_match);
        hsh.complete_merge_handle(rc);
        return hsh;
    }
}
