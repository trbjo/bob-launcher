namespace BobLauncher {

    public class FileMatch : Match, IFile, IRichDescription {
        private static unowned AppSettings.UI ui_settings;

        static construct {
            ui_settings = AppSettings.get_default().ui;
        }

        construct {
            ui_settings.accent_color_changed.connect(rehighlight_matches);
        }

        private void rehighlight_matches() {
            this.descriptions = null;
        }


        private FileMatch() { }

        private string _title;
        public override string get_title() {
            if (this._title == null) {
                _title = get_base_name(this.filename);
            }
            return _title;
        }

        private GenericArray<Description> descriptions;

        public override string get_description() {
            assert_not_reached();
        }

        public unowned GenericArray<Description> get_rich_description(Levensteihn.StringInfo si) {
            if (this.descriptions == null) {
                descriptions = generate_description_for_file(si, this.filename);
            }
            return descriptions;
        }

        public override string get_icon_name() {
            return IconCacheService.best_icon_name_for_mime_type(this.get_mime_type());
        }

        public static GenericArray<string> split_path_with_separators(string path) {
            var components = new GenericArray<string>();
            var parts = path.split(Path.DIR_SEPARATOR_S);
            for (int i = 1; i < parts.length; i++) { // i == 1: skip root
                var part = parts[i];
                components.add(Path.DIR_SEPARATOR_S);
                components.add(part);
            }
            return components;
        }

        public static GenericArray<Description> generate_description_for_file(Levensteihn.StringInfo si, string file_path) {
            var description_list = new GenericArray<Description>();
            int[] positions;

            var components = split_path_with_separators(file_path);
            var path_builder = new StringBuilder();
            string? highlight_color = Highlight.get_pango_accent();

            if (file_path.has_prefix(Environment.get_home_dir())) {
                uint home_dir_length = Environment.get_home_dir().length;
                while (path_builder.len < home_dir_length) {
                    path_builder.append(components.steal_index(0));
                }

                positions = Highlight.match_positions_with_markup(si, file_path.substring(path_builder.len));
                string home = "file://" + path_builder.str;
                Description desc = new Description("user-home-symbolic", FragmentType.IMAGE, () => Utils.launch_uri(home));
                description_list.add(desc);
            } else {
                positions = Highlight.match_positions_with_markup(si, file_path);
                Description desc = new Description("drive-harddisk-symbolic", FragmentType.IMAGE, () => Utils.launch_uri("file:///"));
                description_list.add(desc);
            }

            for (int i = 0; i < components.length; i++) {
                string highlighted = Highlight.apply_highlights(components[i], highlight_color, positions);
                path_builder.append(components[i]);

                Description desc;
                if (components[i] == Path.DIR_SEPARATOR_S) {
                    desc = new Description("path-separator-symbolic", FragmentType.IMAGE, null);
                } else {
                    var target = "file://" + path_builder.str;
                    desc = new Description(highlighted, FragmentType.TEXT, () => Utils.launch_uri(target));
                }
                description_list.add(desc);

                // After processing this component, shift down positions and remove consumed ones
                int consumed_chars = components[i].char_count();
                int first_valid = 0;
                while (first_valid < positions.length && positions[first_valid] < consumed_chars) {
                    first_valid++;
                }
                if (first_valid > 0) {
                    positions = positions[first_valid:positions.length];
                }

                // Shift remaining positions
                for (int j = 0; j < positions.length; j++) {
                    positions[j] -= consumed_chars;
                }
            }

            return (owned)description_list;
        }


        private File? _file = null;
        public File get_file() {
            if (_file == null) {
                _file = File.new_for_path(this.filename);
            }
            return _file;
        }

        public string get_file_path() {
            if (_file == null) {
                _file = File.new_for_path(this.filename);
            }
            return _file.get_path();
        }

        public string get_uri() {
            if (_file == null) {
                _file = File.new_for_path(this.filename);
            }
            return _file.get_uri();
        }

        public string filename { get; construct; }

        private string? _mime_type = null;
        public string get_mime_type() {
            if (_mime_type == null) {
                _mime_type =  "application/x-unknown";
                var f = get_file();
                try {
                    FileInfo file_info = f.query_info("standard::content-type", GLib.FileQueryInfoFlags.NONE);
                    string content_type = file_info.get_content_type();
                    _mime_type = GLib.ContentType.get_mime_type(content_type);
                } catch (Error e) {
                    debug("Failed to guess MIME type for URI %s: %s", this.filename, e.message);
                }
            }
            return _mime_type;
        }

        private static string get_base_name(string path) {
            return Path.get_basename(path);
        }

        public FileMatch.from_path(string filename) {
            Object(filename: filename);
        }

        public FileMatch.from_uri(string uri) {
            try {
                string _filename = GLib.Filename.from_uri(uri);
                Object(filename: _filename);
            } catch (Error e) {
                warning("could not resolve uri: %s, error: %s", uri, e.message);
            }
        }

    }
}
