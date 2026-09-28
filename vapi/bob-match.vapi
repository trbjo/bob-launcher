namespace BobLauncher {
    [CCode (cheader_filename = "bob-match.h", cname = "BobLauncherMatch", type_id = "bob_launcher_match_get_type()")]
    public abstract class Match : GLib.Object {
        [CCode (cname = "bob_launcher_match_construct")]
        protected Match();

        [CCode (cname = "bob_launcher_match_get_title")]
        public abstract string get_title();

        [CCode (cname = "bob_launcher_match_get_icon_name")]
        public abstract string get_icon_name();

        [CCode (cname = "bob_launcher_match_get_description")]
        public abstract string get_description();

        [CCode (cname = "bob_launcher_match_get_tooltip")]
        public virtual unowned Gtk.Widget? get_tooltip();
    }

    [CCode (cheader_filename = "bob-match.h", cname = "BobLauncherSourceMatch", type_id = "bob_launcher_source_match_get_type()")]
    public abstract class SourceMatch : Match {
        [CCode (cname = "bob_launcher_source_match_construct")]
        protected SourceMatch();

        public signal void executed(bool success);
    }

    [CCode (cheader_filename = "bob-match.h", cname = "BobLauncherAction", type_id = "bob_launcher_action_get_type()")]
    public abstract class Action : Match {
        [CCode (cname = "bob_launcher_action_construct")]
        protected Action();

        [CCode (cname = "bob_launcher_action_do_execute")]
        protected abstract bool do_execute(Match source, Match? target = null);

        [CCode (cname = "bob_launcher_action_execute")]
        public virtual bool execute(Match source, Match? target = null);

        [CCode (cname = "bob_launcher_action_get_relevancy")]
        public abstract Score get_relevancy(Match m);
    }

    [CCode (cheader_filename = "bob-match.h", cname = "BobLauncherActionTarget", type_id = "bob_launcher_action_target_get_type()")]
    public abstract class ActionTarget : Action {
        [CCode (cname = "bob_launcher_action_target_construct")]
        protected ActionTarget();

        [CCode (cname = "bob_launcher_action_target_target_match")]
        public abstract Match target_match(string query);
    }
}
