namespace BobLauncher {
    public interface IFile : GLib.Object {
        public abstract string get_file_path();
        public abstract string get_uri();
        public abstract string get_mime_type();
        public abstract File get_file();
    }

    internal interface ILaunchContext : GLib.Object {
        public abstract bool launch_files(List<File> files, string[]? env = null);
        public abstract bool launch_uris(List<string> uris, string[]? env = null);
        public abstract bool launch_with_files(AppInfo app_info, List<File>? files = null, string? action = null, string[]? env = null);
        public abstract bool launch_with_uris(AppInfo app_info, List<string>? uris = null, string? action = null, string[]? env = null);
        public abstract bool launch_command(string identifier, string[] argv, string[]? env = null, bool blocking = false, bool needs_terminal = false);
    }

    public interface ITextMatch : GLib.Object {
        public abstract string get_text();
    }

    public interface IActionMatch : GLib.Object {
        public abstract bool do_action();
    }

    public interface IRichDescription : Match {
        public abstract unowned Description get_rich_description(Levensteihn.StringInfo si);
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
