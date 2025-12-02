namespace BobLauncher {
    public class FileMatch : Match, IFile, IRichDescription {
        public const string SEARCH_FILE_ATTRIBUTES =
            FileAttribute.STANDARD_NAME + "," +
            FileAttribute.STANDARD_DISPLAY_NAME + "," +
            FileAttribute.STANDARD_DESCRIPTION + "," +
            FileAttribute.STANDARD_TYPE + "," +
            FileAttribute.STANDARD_CONTENT_TYPE + "," +
            FileAttribute.STANDARD_SIZE + "," +
            FileAttribute.STANDARD_ICON + "," +
            FileAttribute.TIME_CREATED + "," +
            FileAttribute.TIME_MODIFIED + "," +
            FileAttribute.RECENT_MODIFIED + "," +
            FileAttribute.UNIX_MODE + "," +
            FileAttribute.STANDARD_IS_SYMLINK + "," +
            FileAttribute.STANDARD_SYMLINK_TARGET + "," +
            FileAttribute.OWNER_USER + "," +
            FileAttribute.OWNER_GROUP + "," +
            FileAttribute.THUMBNAIL_PATH_XXLARGE + "," +
            FileAttribute.THUMBNAILING_FAILED;


        public override unowned Gtk.Widget? get_tooltip() {
            if (_tooltip_widget != null) {
                return _tooltip_widget;
            }

            var file = get_file();
            if (!file.query_exists()) {
                return null;
            }

            var box = new Gtk.Box(Gtk.Orientation.VERTICAL, 0);
            string mime_type = get_mime_type();

            try {
                FileInfo file_info = file.query_info(SEARCH_FILE_ATTRIBUTES,
                    FileQueryInfoFlags.NONE
                );

                var file_title = new Gtk.Label(file.get_basename()){
                    css_classes = {"tooltip-title"}
                };

                file_title.xalign = 0.5f;
                file_title.ellipsize = Pango.EllipsizeMode.MIDDLE;
                box.append(file_title);

                if (mime_type.has_prefix("image/")) {
                    handle_image_tooltip(box, file, file_info);
                } else if (mime_type.has_prefix("text/") || mime_type == "application/json" || mime_type == "application/xml") {
                    handle_text_tooltip(box, file, file_info);
                } else if (mime_type.has_prefix("audio/")) {
                    handle_audio_tooltip(box, file, file_info);
                } else if (mime_type.has_prefix("video/")) {
                    handle_video_tooltip(box, file, file_info);
                } else if (mime_type.has_prefix("application/zip") || mime_type.has_prefix("application/x-tar")) {
                    handle_archive_tooltip(box, file, file_info);
                } else {
                    handle_generic_tooltip(box, file, file_info, mime_type);
                }

            } catch (Error e) {
                var error_label = new Gtk.Label("Error reading file: %s".printf(e.message));
                error_label.xalign = 0;
                box.append(error_label);
            }

            _tooltip_widget = box;
            return _tooltip_widget;
        }

        private Gdk.Paintable? set_image_from_file(string path) {
            try {
                return Gdk.Texture.from_filename(path);
            } catch (Error e) {
                warning("Error loading image: %s", e.message);
            }
            return null;
        }

        private void calculate_dimensions(int original_width, int original_height, int max_size, out int new_width, out int new_height) {
            if (original_width > original_height) {
                new_width = int.min(original_width, max_size);
                new_height = (int)((double)original_height / original_width * new_width);
            } else {
                new_height = int.min(original_height, max_size);
                new_width = (int)((double)original_width / original_height * new_height);
            }
        }

        private void handle_image_tooltip(Gtk.Box box, File file, FileInfo file_info) {
            box.append(GLib.Object.new(typeof(PaintableWidgetWrapper), "file_info", file_info, "file", file) as PaintableWidgetWrapper);
        }

        private void handle_text_tooltip(Gtk.Box box, File file, FileInfo file_info) {
            try {
                var input_stream = file.read();
                var buffer = new uint8[1024];
                size_t bytes_read = input_stream.read(buffer);
                input_stream.close();

                if (bytes_read > 0) {
                    string preview = (string)buffer[0:bytes_read];
                    string[] lines = preview.split("\n");
                    int preview_lines = int.min(lines.length, 6);

                    var preview_text = new StringBuilder();
                    for (int i = 0; i < preview_lines; i++) {
                        string line = lines[i];
                        if (line.length > 70) {
                            line = line.substring(0, 67) + "...";
                        }
                        preview_text.append(line);
                        if (i < preview_lines - 1) preview_text.append("\n");
                    }

                    var text_label = new Gtk.Label(preview_text.str);
                    text_label.xalign = 0;
                    text_label.yalign = 0;
                    text_label.max_width_chars = 70;
                    text_label.wrap = true;
                    text_label.use_markup = false;
                    text_label.add_css_class("monospace");
                    box.append(text_label);

                    var line_count = count_lines_in_file(file);
                    if (line_count > 0) {
                        var lines_label = new Gtk.Label("%d lines".printf(line_count));
                        lines_label.xalign = 0;
                        lines_label.add_css_class("dim-label");
                        box.append(lines_label);
                    }
                }
            } catch (Error e) {
                var error_label = new Gtk.Label("Cannot read text");
                error_label.xalign = 0;
                box.append(error_label);
            }

            add_size_and_time(box, file_info);
        }

        private void handle_audio_tooltip(Gtk.Box box, File file, FileInfo file_info) {
            var audio_label = new Gtk.Label("Audio file");
            audio_label.xalign = 0;
            box.append(audio_label);

            add_size_and_time(box, file_info);
        }

        private void handle_video_tooltip(Gtk.Box box, File file, FileInfo file_info) {
            var video_label = new Gtk.Label("Video file");
            video_label.xalign = 0;
            box.append(video_label);

            add_size_and_time(box, file_info);
        }

        private void handle_archive_tooltip(Gtk.Box box, File file, FileInfo file_info) {
            var archive_label = new Gtk.Label("Archive");
            archive_label.xalign = 0;
            box.append(archive_label);

            add_size_and_time(box, file_info);
        }

        private void handle_generic_tooltip(Gtk.Box box, File file, FileInfo file_info, string mime_type) {
            var type_label = new Gtk.Label(mime_type);
            type_label.xalign = 0;
            box.append(type_label);

            add_size_and_time(box, file_info);
        }

        private void add_size_and_time(Gtk.Box box, FileInfo file_info) {
            if (file_info.has_attribute(FileAttribute.STANDARD_SIZE)) {
                int64 size = file_info.get_size();
                var size_label = new Gtk.Label(format_size(size));
                size_label.xalign = 0;
                box.append(size_label);
            }

            if (file_info.has_attribute(FileAttribute.TIME_MODIFIED)) {
                var modified = file_info.get_modification_date_time();
                var now = new GLib.DateTime.now_local();
                string formatted_time = Utils.format_modification_time(now, modified);
                var time_label = new Gtk.Label(formatted_time);
                time_label.xalign = 0;
                box.append(time_label);
            }
        }

        private int count_lines_in_file(File file) {
            try {
                var input_stream = file.read();
                var data_stream = new DataInputStream(input_stream);
                int line_count = 0;
                string? line;

                while ((line = data_stream.read_line()) != null) {
                    line_count++;
                    if (line_count > 5000) break;
                }

                data_stream.close();
                input_stream.close();
                return line_count;
            } catch (Error e) {
                return -1;
            }
        }




        private static unowned AppSettings.UI ui_settings;
        private static Highlight.Style highlight_style = Highlight.Style.COLOR;

        static construct {
            ui_settings = AppSettings.get_default().ui;

            // Load highlight style from settings
            var settings = new GLib.Settings(BOB_LAUNCHER_APP_ID + ".ui");
            var style_string = settings.get_string("highlight-style");
            highlight_style = parse_highlight_style(style_string);
            settings.changed["highlight-style"].connect(() => {
                var new_style = settings.get_string("highlight-style");
                highlight_style = parse_highlight_style(new_style);
            });
        }

        private static Highlight.Style parse_highlight_style(string style) {
            switch (style) {
                case "underline":
                    return Highlight.Style.UNDERLINE;
                case "bold":
                    return Highlight.Style.BOLD;
                case "background":
                    return Highlight.Style.BACKGROUND;
                case "bold-underline":
                    return Highlight.Style.BOLD | Highlight.Style.UNDERLINE;
                default:
                    return Highlight.Style.COLOR;
            }
        }

        construct {
            ui_settings.accent_color_changed.connect(rehighlight_matches);
        }

        private Gtk.Widget? _tooltip_widget = null;

        private void rehighlight_matches() {
            this.description = null;
        }

        private FileMatch() { }

        private string _title;
        public override string get_title() {
            if (this._title == null) {
                _title = get_base_name(this.filename);
            }
            return _title;
        }

        public override string get_icon_name() {
            return IconCacheService.best_icon_name_for_mime_type(this.get_mime_type());
        }

        private Description description;

        public override string get_description() {
            assert_not_reached();
        }

        public unowned Description get_rich_description(Levensteihn.StringInfo si) {
            if (this.description == null) {
                description = generate_description_for_file(si, this.filename, this.timestamp);
            }
            return description;
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

        public static Description generate_description_for_file(Levensteihn.StringInfo si, string file_path, GLib.DateTime? timestamp) {
            var root = new Description.container("file-description");

            if (timestamp != null) {
                var timestamp_group = new Description.container("timestamp-group", Gtk.Orientation.HORIZONTAL);

                var separator = new Description("tools-timer-symbolic", "timestamp-image", FragmentType.IMAGE, null, null);
                timestamp_group.add_child(separator);

                var now = new GLib.DateTime.now_local();
                string formatted_time = Utils.format_modification_time(now, timestamp);
                var time_desc = new Description(formatted_time, "timestamp", FragmentType.TEXT, null, null);
                timestamp_group.add_child(time_desc);

                root.add_child(timestamp_group);
            }

            var path_group = new Description.container("path-group", Gtk.Orientation.HORIZONTAL);

            var components = split_path_with_separators(file_path);
            var path_builder = new StringBuilder();
            unowned Gdk.RGBA accent_color = Highlight.get_accent_color();

            string search_path;
            int byte_offset = 0;

            Highlight.Positions positions;
            if (file_path.has_prefix(Environment.get_home_dir())) {
                uint home_dir_length = Environment.get_home_dir().length;
                while (path_builder.len < home_dir_length) {
                    path_builder.append(components.steal_index(0));
                }
                search_path = file_path.substring(path_builder.len);
                byte_offset = (int)path_builder.len;

                string home = "file://" + path_builder.str;
                var home_icon = new Description("user-home-symbolic", "image", FragmentType.IMAGE,
                                               () => BobLaunchContext.get_instance().launch_uri(home), null);
                path_group.add_child(home_icon);
            } else {
                search_path = file_path;
                var root_icon = new Description("drive-harddisk-symbolic", "image", FragmentType.IMAGE,
                                               () => BobLaunchContext.get_instance().launch_uri("file:///"), null);
                path_group.add_child(root_icon);
            }

            positions = new Highlight.Positions(si, search_path);
            int search_path_offset = 0;

            for (int i = 0; i < components.length; i++) {
                string component = components[i];
                path_builder.append(component);

                Description desc;
                if (component == Path.DIR_SEPARATOR_S) {
                    desc = new Description("path-separator-symbolic", "image", FragmentType.IMAGE, null, null);
                } else {
                    var attrs = Highlight.apply_style_range(positions, highlight_style, accent_color,
                                                           search_path_offset, search_path_offset + component.length);

                    var target = "file://" + path_builder.str;
                    desc = new Description(component, "path-fragment", FragmentType.TEXT,
                                         () => BobLaunchContext.get_instance().launch_uri(target), attrs);
                }
                search_path_offset += component.length;

                path_group.add_child(desc);
            }

            root.add_child(path_group);
            return root;
        }

        private File? _file = null;
        public File get_file() {
            if (_file == null) {
                _file = File.new_for_path(this.filename);
            }
            return _file;
        }

        private FileInfo? _file_info = null;

        public FileInfo get_file_info() {
            if (_file_info == null) {
                var f = get_file();
                _file_info = f.query_info(SEARCH_FILE_ATTRIBUTES, FileQueryInfoFlags.NONE);
            }
            return _file_info;
        }

        public string get_file_path() {
            if (_file == null) {
                _file = File.new_for_path(this.filename);
            }
            return _file.get_path();
        }

        public bool is_directory() {
            return get_file_info().get_file_type() == FileType.DIRECTORY;
        }

        public string get_uri() {
            if (_file == null) {
                _file = File.new_for_path(this.filename);
            }
            return _file.get_uri();
        }

        public string filename { get; construct; }
        private GLib.DateTime? _timestamp = null;
        public GLib.DateTime timestamp {
            get {
                if (_timestamp == null) {
                    FileInfo fi = get_file_info();
                    if (_timestamp == null && fi.has_attribute(FileAttribute.TIME_ACCESS)) {
                        _timestamp = fi.get_access_date_time();
                    }

                    if (_timestamp == null && fi.has_attribute(FileAttribute.TIME_MODIFIED)) {
                        _timestamp = fi.get_modification_date_time();
                    }
                }
                return _timestamp;
            }
            set {
                _timestamp = value;
            }
        }

        private string? _mime_type = null;
        public string get_mime_type() {
            if (_mime_type == null) {
                _mime_type = "application/x-unknown";
                FileInfo fi = get_file_info();
                string content_type = fi.get_content_type();
                _mime_type = GLib.ContentType.get_mime_type(content_type);
            }
            return _mime_type;
        }

        private static string get_base_name(string path) {
            return Path.get_basename(path);
        }

        protected override void dispose() {
            if (_tooltip_widget != null) {
                _tooltip_widget = null;
            }
            base.dispose();
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
