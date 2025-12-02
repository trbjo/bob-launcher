namespace BobLauncher {

    public enum FragmentType {
        IMAGE,
        TEXT,
        CONTAINER
    }

    public class Description : Object {
        public string text;
        public string css_class;
        public FragmentType fragment_type;
        public GenericArray<Description>? children;

        public FragmentFunc? fragment_func;
        public Pango.AttrList? attributes;

        public Gtk.Orientation orientation = Gtk.Orientation.HORIZONTAL;

        public Description(
                            string text,
                            string css_class,
                            FragmentType fragment_type = FragmentType.TEXT,
                            owned FragmentFunc? fragment_func = null,
                            Pango.AttrList? attrs = null) {
            this.text = text;
            this.css_class = css_class;
            this.fragment_type = fragment_type;
            if (fragment_func != null) {
                this.fragment_func = (owned)fragment_func;
            }
            if (attrs != null) {
                this.attributes = attrs;
            }

            this.children = null;
        }

        public Description.container(string css_class = "", Gtk.Orientation orientation = Gtk.Orientation.HORIZONTAL,
                                     owned FragmentFunc? fragment_func = null) {
            this.text = "";
            this.css_class = css_class;
            this.fragment_type = FragmentType.CONTAINER;
            if (fragment_func != null) {
                this.fragment_func = (owned)fragment_func;
            }
            this.children = new GenericArray<Description>();
            this.orientation = orientation;
        }

        public void add_child(Description child) {
            if (children == null) {
                children = new GenericArray<Description>();
            }
            children.add(child);
        }
    }

    public delegate void FragmentFunc() throws Error;

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
