[CCode (cheader_filename = "plugin-base.h")]
namespace BobLauncher {
    [CCode (type_id = "BOB_LAUNCHER_TYPE_PLUGIN_BASE",
            cname = "BobLauncherPluginBase",
            cprefix = "bob_launcher_plugin_base_")]
    public abstract class PluginBase : Match {
        [CCode (has_construct_function = false)]
        protected PluginBase();

        public double bonus { get; set; }

        [CCode (cname = "bob_launcher_plugin_base_is_enabled")]
        public bool is_enabled();

        [CCode (cname = "bob_launcher_plugin_base_make_match")]
        public MatchFactory make_match();

        [CCode (cname = "bob_launcher_plugin_base_initialize")]
        public virtual ulong initialize(GLib.Settings settings);

        [CCode (cname = "bob_launcher_plugin_base_shutdown")]
        public void shutdown();

        [CCode (cname = "bob_launcher_plugin_base_get_mime_type")]
        public string get_mime_type();

        [CCode (cname = "bob_launcher_plugin_base_to_string")]
        public virtual string to_string();

        [CCode (vfunc_name = "activate")]
        protected virtual bool activate();

        [CCode (vfunc_name = "deactivate")]
        protected virtual void deactivate();

        [CCode (vfunc_name = "on_setting_changed")]
        public virtual void on_setting_changed(string key, GLib.Variant value);

        [CCode (vfunc_name = "find_for_match")]
        public virtual void find_for_match(Match match, ActionSet rs);

        [CCode (vfunc_name = "get_title")]
        public abstract string get_title();

        [CCode (vfunc_name = "get_description")]
        public abstract string get_description();

        [CCode (vfunc_name = "get_icon_name")]
        public abstract string get_icon_name();
    }
}
