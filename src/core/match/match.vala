namespace BobLauncher {
    public abstract class SourceMatch : Match {
        public signal void executed(bool success);
        protected SourceMatch() { }
    }

    public abstract class Match : GLib.Object {
        protected Match() { }
        public abstract string get_title();
        public abstract string get_icon_name();
        public abstract string get_description();
        public virtual unowned Gtk.Widget? get_tooltip() {
            return null;
        }
    }

    public abstract class ActionTarget : Action {
        protected ActionTarget() { }
        public abstract Match target_match(string query);
    }

    public abstract class Action : Match {
        protected Action() { }

        protected abstract bool do_execute(Match source, Match? target = null);
        public virtual bool execute(Match source, Match? target = null) {
            var success = do_execute(source, target);
            if (source is SourceMatch) source.executed(success);
            return success;
        }
        public abstract Score get_relevancy(Match m);
    }
}
