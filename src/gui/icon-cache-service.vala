namespace BobLauncher {
    namespace IconCacheService {
        // Single lock for all operations
        private static Spinlock.AtomicInt lock_token;

        private static GLib.HashTable<string, string> mime_type_map;
        private static GLib.HashTable<uint, Gdk.Paintable> icon_cache;
        private static Gtk.IconTheme theme;

        private const string FALLBACK = "image-missing";

        internal static void initialize() {
            icon_cache = new GLib.HashTable<uint, Gdk.Paintable>(direct_hash, direct_equal);
            mime_type_map = new GLib.HashTable<string, string>(str_hash, str_equal);

            theme = Gtk.IconTheme.get_for_display(Gdk.Display.get_default());
            theme.add_resource_path(BOB_LAUNCHER_OBJECT_PATH);
            theme.changed.connect(clear_cache);
        }

        private static void clear_cache() {
            while (!Spinlock.spin_lock(ref lock_token));
            mime_type_map.remove_all();
            icon_cache.remove_all();
            Spinlock.spin_unlock(ref lock_token);
        }

        private static uint compute_composite_hash(string str, int size, int scale_factor) {
            // Get the string hash
            uint str_hash = str.hash();

            // Ensure size is positive and within reasonable bounds
            assert(size > 0 && size <= 512);

            // Ensure scale factor is within reasonable bounds (1-4)
            assert(scale_factor > 0 && scale_factor <= 4);

            // Reserve:
            // - Lower 9 bits for size (allows sizes up to 512)
            // - Next 2 bits for scale factor (allows values 1-4)
            // - Upper 21 bits for string hash
            uint size_part = (uint)(size & 0x1FF);        // Take lower 9 bits of size
            uint scale_part = (uint)((scale_factor - 1) & 0x3) << 9;   // Take 2 bits, shifted left by 9
            uint str_part = (str_hash >> 11) << 11;       // Clear lower 11 bits, shifted back

            return str_part | scale_part | size_part;
        }

        public static unowned Gdk.Paintable get_paintable_for_icon_name(string icon_name, int size, int scale) {
            while (!Spinlock.spin_lock(ref lock_token));

            uint key = compute_composite_hash(icon_name, size, scale);
            unowned Gdk.Paintable? p = icon_cache.get(key);

            if (p == null) {
                string lookup_str;
                bool has_icon = theme.has_icon(icon_name);
                lookup_str = has_icon ? icon_name : FALLBACK;

                var paintable = theme.lookup_icon(lookup_str, null, size, scale, Gtk.TextDirection.NONE, 0);
                p = paintable;
                icon_cache.set(key, paintable);
            }

            Spinlock.spin_unlock(ref lock_token);
            return p;
        }

        public static string best_icon_name_for_mime_type(string? content_type) {
            if (content_type == null) return FALLBACK;

            while (!Spinlock.spin_lock(ref lock_token));

            string? mime_icon = mime_type_map.get(content_type);
            if (mime_icon != null) {
                Spinlock.spin_unlock(ref lock_token);
                return mime_icon;
            }

            GLib.ThemedIcon? icon = GLib.ContentType.get_icon(content_type) as GLib.ThemedIcon;

            if (icon == null) {
                warning("match not found: %s", content_type);
                Spinlock.spin_unlock(ref lock_token);
                return FALLBACK;
            }

            var names = icon.get_names();
            string result = FALLBACK;

            foreach (var name in names) {
                if (theme.has_icon(name)) {
                    result = name;
                    break;
                }
            }

            mime_type_map.set(content_type, result);

            Spinlock.spin_unlock(ref lock_token);
            return result;
        }
    }
}
