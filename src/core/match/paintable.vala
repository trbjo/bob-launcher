namespace BobLauncher {
    public class PaintableWidgetWrapper : Gtk.Widget {
        private static Gtk.IconTheme icon_theme;
        private static Gdk.Paintable checkerboard;


        private double aspect_ratio;
        private double scaled_width;
        private double scaled_height;
        public File file { get; construct; }
        public FileInfo file_info { get; construct set; }
        private Gdk.Paintable paintable;

        static construct {
            checkerboard = create_checkerboard(512);
            icon_theme = Gtk.IconTheme.get_for_display(Gdk.Display.get_default());
        }

        construct {
            overflow = Gtk.Overflow.HIDDEN;
            valign = Gtk.Align.CENTER;
            halign = Gtk.Align.CENTER;

            add_css_class("thumbnail-widget");
            load();
        }

        protected override void dispose() {
            base.dispose();
        }

        public void load() {
            _load();
            if (paintable == null) {
                paintable = checkerboard;
            }
            aspect_ratio = paintable.get_intrinsic_aspect_ratio();
            scaled_width = paintable.get_intrinsic_width() / ((double)scale_factor);
            scaled_height = paintable.get_intrinsic_height() / ((double)scale_factor);
            update_alpha_status();
        }

        public Gdk.Paintable? icon_to_paintable(GLib.Icon icon, int size = 256) {
            return icon_theme.lookup_by_gicon(icon, size, 1, Gtk.TextDirection.NONE, Gtk.IconLookupFlags.FORCE_REGULAR);
        }

        private void _load() {
            var thumbnail_path = file_info.get_attribute_byte_string(FileAttribute.THUMBNAIL_PATH_XXLARGE);
            if (thumbnail_path != null && FileUtils.test(thumbnail_path, FileTest.EXISTS)) {
                paintable = set_image_from_file(thumbnail_path);
            }
            var content_type = file_info.get_content_type();
            if (content_type.has_prefix("image/")) {
                string? path = file.get_path();
                if (path != null) {
                    // Try to create thumbnail
                    if (create_thumbnail(path)) {
                        // Reload file info to get the new thumbnail path
                        try {
                            file_info = file.query_info(FileMatch.SEARCH_FILE_ATTRIBUTES, FileQueryInfoFlags.NONE);
                            thumbnail_path = file_info.get_attribute_byte_string(FileAttribute.THUMBNAIL_PATH_XXLARGE);
                            if (thumbnail_path != null) {
                                paintable = set_image_from_file(thumbnail_path);
                            }
                        } catch (Error e) {
                            warning("Error querying file info: %s", e.message);
                        }
                    }
                    // If thumbnail creation failed or we couldn't get the new path, fall back to original image
                    paintable = set_image_from_file(path);
                } else {
                    paintable = icon_to_paintable(file_info.get_icon());
                }
            } else {
                paintable = icon_to_paintable(file_info.get_icon());
            }
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
                new_width = max_size;
                new_height = (int)((double)original_height / original_width * max_size);
            } else {
                new_height = max_size;
                new_width = (int)((double)original_width / original_height * max_size);
            }
        }



        private bool create_thumbnail(string file_path, int size = 512) {
            return false;
            try {
                // Load the image
                var pixbuf = new Gdk.Pixbuf.from_file(file_path);

                // Resize the image
                int width, height;
                calculate_dimensions(pixbuf.get_width(), pixbuf.get_height(), size, out width, out height);
                var scaled = pixbuf.scale_simple(width, height, Gdk.InterpType.HYPER);

                // Generate the thumbnail filename
                var file = File.new_for_path(file_path);
                var uri = file.get_uri();
                var md5 = GLib.Checksum.compute_for_string(GLib.ChecksumType.MD5, uri);

                var cache_dir = Path.build_filename(Environment.get_user_cache_dir(), "thumbnails", "xx-large");
                var thumb_path = Path.build_filename(cache_dir, md5 + ".png");

                // Ensure the cache directory exists
                DirUtils.create_with_parents(cache_dir, 0755);

                // Save the thumbnail
                scaled.save(thumb_path, "png");

                return true;
            } catch (Error e) {
                warning("Error creating thumbnail: %s", e.message);
                return false;
            }
        }







        public override void measure(Gtk.Orientation orientation,
                                     int for_size,
                                     out int minimum,
                                     out int natural,
                                     out int minimum_baseline,
                                     out int natural_baseline)
        {
            minimum = minimum_baseline = natural_baseline = -1;
            if (((int)scaled_height) > for_size > 0 && orientation == Gtk.Orientation.HORIZONTAL) {
                double downscaled_width = Math.round(for_size * aspect_ratio);
                natural = (int)downscaled_width;
            } else if (((int)scaled_width) > for_size > 0 && orientation == Gtk.Orientation.VERTICAL) {
                double downscaled_height = Math.round(for_size / aspect_ratio);
                natural = (int)downscaled_height;
            } else {
                natural = (int)(orientation == Gtk.Orientation.HORIZONTAL ? scaled_width : scaled_height);
            }
        }

        public override Gtk.SizeRequestMode get_request_mode() {
            return Gtk.SizeRequestMode.WIDTH_FOR_HEIGHT;
        }

        private bool paintable_has_alpha;


        private void update_alpha_status() {
            var current_image = paintable.get_current_image();
            if (current_image is Gdk.Texture) {
                paintable_has_alpha = texture_has_alpha((Gdk.Texture)current_image);
            } else {
                // If it's not a texture, we can't be certain. Assume it might have alpha.
                paintable_has_alpha = true;
            }
        }


        private bool texture_has_alpha(Gdk.Texture texture) {
            Gdk.MemoryFormat format = texture.get_format();
            switch (format) {
                case Gdk.MemoryFormat.B8G8R8A8_PREMULTIPLIED:
                case Gdk.MemoryFormat.A8R8G8B8_PREMULTIPLIED:
                case Gdk.MemoryFormat.R8G8B8A8_PREMULTIPLIED:
                case Gdk.MemoryFormat.B8G8R8A8:
                case Gdk.MemoryFormat.A8R8G8B8:
                case Gdk.MemoryFormat.R8G8B8A8:
                case Gdk.MemoryFormat.A8B8G8R8:
                case Gdk.MemoryFormat.R16G16B16A16_PREMULTIPLIED:
                case Gdk.MemoryFormat.R16G16B16A16:
                case Gdk.MemoryFormat.R16G16B16A16_FLOAT_PREMULTIPLIED:
                case Gdk.MemoryFormat.R16G16B16A16_FLOAT:
                case Gdk.MemoryFormat.R32G32B32A32_FLOAT_PREMULTIPLIED:
                case Gdk.MemoryFormat.R32G32B32A32_FLOAT:
                case Gdk.MemoryFormat.G8A8_PREMULTIPLIED:
                case Gdk.MemoryFormat.G8A8:
                case Gdk.MemoryFormat.G16A16_PREMULTIPLIED:
                case Gdk.MemoryFormat.G16A16:
                case Gdk.MemoryFormat.A8:
                case Gdk.MemoryFormat.A16:
                case Gdk.MemoryFormat.A16_FLOAT:
                case Gdk.MemoryFormat.A32_FLOAT:
                case Gdk.MemoryFormat.A8B8G8R8_PREMULTIPLIED:
                    return true;
                default:
                    return false;
            }
        }

        public override void snapshot(Gtk.Snapshot snapshot) {
            snapshot.save();

            if (paintable_has_alpha) {
                checkerboard.snapshot(snapshot, get_width(), get_height());
            }

            paintable.snapshot(snapshot, get_width(), get_height());
            snapshot.restore();
        }

        private static Gdk.Paintable create_checkerboard(int size_at_scale) {
            Graphene.Size dimensions = Graphene.Size() {
               width = size_at_scale,
               height = size_at_scale
            };
            var snapshot = new Gtk.Snapshot();
            var rect = Graphene.Rect().init(0, 0, size_at_scale, size_at_scale);

            snapshot.append_color(Gdk.RGBA() { red = 0.9f, green = 0.9f, blue = 0.9f, alpha = 0.7f }, rect);

            int tile_size = size_at_scale / 16;
            for (int y = 0; y < size_at_scale; y += tile_size * 2) {
                for (int x = 0; x < size_at_scale; x += tile_size * 2) {
                    snapshot.push_clip(Graphene.Rect().init(x, y, tile_size, tile_size));
                    snapshot.append_color(Gdk.RGBA() { red = 0.8f, green = 0.8f, blue = 0.8f, alpha = 0.7f }, rect);
                    snapshot.pop();

                    snapshot.push_clip(Graphene.Rect().init(x + tile_size, y + tile_size, tile_size, tile_size));
                    snapshot.append_color(Gdk.RGBA() { red = 0.8f, green = 0.8f, blue = 0.8f, alpha = 0.7f }, rect);
                    snapshot.pop();
                }
            }
            return snapshot.to_paintable(dimensions);
        }
    }
}
