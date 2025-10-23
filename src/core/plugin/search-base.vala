namespace BobLauncher {
    public abstract class SearchBase : PluginBase {
        public virtual bool prefer_insertion_order { get { return false; } }
        public virtual uint shard_count { get; set; default = 1; }
        public uint update_interval { get; set; }
        public bool enabled_in_default_search { get; set; }
        private string _regex_match = "^$";
        private GLib.Regex _compiled_regex;

        public string regex_match {
            get { return _regex_match; }
            set {
                if (_regex_match != value && update_compiled_regex(value)) {
                    _regex_match = value;
                }
            }
        }

        public GLib.Regex compiled_regex {
            get { return _compiled_regex; }
        }

        private bool update_compiled_regex(string new_value) {
            try {
                _compiled_regex = new GLib.Regex(new_value, GLib.RegexCompileFlags.OPTIMIZE, 0);
                return true;
            } catch (GLib.RegexError e) {
                warning("Failed to compile regex '%s', reusing existing: %s: %s", new_value, _regex_match, e.message);
                return false;
            }
        }

        internal void handle_base_settings(string key, GLib.Variant value) {
            switch (key) {
            case "regex-match":
                this.regex_match = value.get_string();
                break;
            case "enabled-in-default":
                this.enabled_in_default_search = value.get_boolean();
                break;
            case "update-interval":
                uint user_value = value.get_uint32();
                update_interval = ((user_value == 0) ? 0 : uint.max(500, user_value) * 1000);
                break;
            }
        }

        protected virtual void search(ResultContainer rs) {
            error("Plugin has not implemented search");
        }

        public virtual void search_shard(ResultContainer rs, uint shard_id) {
            if (shard_id > 0) {
                error("Plugin doesn't support sharding");
            }
            search(rs);
        }
    }
}
