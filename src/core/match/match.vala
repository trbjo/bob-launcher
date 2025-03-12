namespace BobLauncher {

    public enum FragmentType {
        IMAGE,
        TEXT
    }

    public class Description {
        public string text;
        public FragmentType fragment_type;
        public FragmentFunc? fragment_func;

        public Description(string text, FragmentType fragment_type = FragmentType.TEXT, owned FragmentFunc? fragment_func = null) {
            this.text = text;
            this.fragment_type = fragment_type;
            if (fragment_func != null) {
                this.fragment_func = (owned)fragment_func;
            }
        }
    }

    public class FragmentAction : Object {
        public FragmentFunc func;
        public FragmentAction(owned FragmentFunc func) {
            this.func = (owned)func;
        }
    }

    public delegate void FragmentFunc() throws Error;

    public abstract class SourceMatch : Match {
        public signal void executed();
        protected SourceMatch() { }
    }

    public abstract class Match : GLib.Object {
        protected Match() { }
        public abstract string get_title();
        public abstract string get_icon_name();
        public abstract string get_description();
        public virtual Gtk.Widget? get_tooltip() {
            return null;
        }
    }

    public abstract class ActionTarget : Action {
        protected ActionTarget() { }
    }

    public abstract class Action : Match {
        protected Action() { }

        protected abstract bool do_execute(Match source, Match? target = null);
        public virtual bool execute(Match source, Match? target = null) {
            var retval = do_execute(source, target);
            if (source is SourceMatch) source.executed();
            return retval;
        }
        public abstract Score get_relevancy(Match m);
    }
}
