namespace BobLauncher {
    [CCode (cheader_filename = "plugin-base.h", type_id = "bob_launcher_plugin_base_get_type ()")]
    public abstract class PluginBase : Match {
        [CCode (cname = "title")]
        public string title;
        [CCode (cname = "description")]
        public string description;
        [CCode (cname = "icon_name")]
        protected string icon_name;

        public int16 bonus { get; set; }
        public bool enabled { get; set; }
        public GLib.GenericArray<SearchBase> search_providers { get; protected set; }

        public virtual bool activate();
        public virtual void deactivate();
        public virtual void on_setting_changed(string key, GLib.Variant value);
        public virtual string to_string();
        public virtual void find_for_match(Match match, ActionSet rs);
        public string get_mime_type();
    }

    [CCode (cheader_filename = "search-base.h", type_id = "bob_launcher_search_base_get_type ()")]
    public abstract class SearchBase : PluginBase {
        public virtual uint shard_count { get; set; }
        public uint update_interval { get; set; }
        public bool enabled_in_default_search { get; set; }
        public string regex_match { get; set; }
        public GLib.Regex compiled_regex { get; }

        [CCode (cname = "bob_launcher_search_base_handle_base_settings")]
        public void handle_base_settings(string key, GLib.Variant value);

        protected virtual void search(ResultContainer rs);
        public virtual void search_shard(ResultContainer rs, uint shard_id);
    }
}
