namespace BobLauncher {
    public interface IFile: GLib.Object {
        public abstract string get_file_path();
        public abstract string get_uri();
        public abstract string get_mime_type();
        public abstract File get_file();
    }

    public interface ITextMatch : GLib.Object {
        public abstract string get_text();
    }

    public interface IActionMatch : GLib.Object {
        public abstract void do_action();
    }

    public interface IRichDescription : Match {
        public abstract unowned GenericArray<Description> get_rich_description(Levensteihn.StringInfo si);
    }

    public interface IURLMatch : GLib.Object {
        public abstract string get_url();
    }

    public interface IDesktopApplication : GLib.Object {
        public abstract bool needs_terminal();
        public abstract unowned GenericArray<Action> get_actions();
        public abstract unowned DesktopAppInfo get_desktop_appinfo();
    }
}
