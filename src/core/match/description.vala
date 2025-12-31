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
}
