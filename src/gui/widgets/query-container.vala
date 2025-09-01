namespace BobLauncher {
    internal class QueryContainer : Gtk.Widget {
        private class CursorWidget : Gtk.Widget {
            construct {
                name = "text-cursor";
            }
        }

        private const string TYPE_TO_SEARCH = "Type to search\u2026";
        private const string SELECT_PLUGIN = "Select a plugin\u2026";

        private static Graphene.Matrix color_matrix;
        private static Graphene.Vec4 color_offset;
        private static Pango.Layout[] layouts;
        private static TextRepr text_repr;
        private static unowned QueryContainer instance;
        private int selected_row_height = 0;
        private CursorWidget cursor_w;
        private int cursor_position;

        private enum TextRepr {
            FALLBACK_PLG =  0,
            PLG =  1,
            FALLBACK_SRC =  2,
            SRC =  3,
            FALLBACK_ACT =  4,
            ACT =  5,
            FALLBACK_TAR =  6,
            TAR =  7,
            EMPTY =  8,
            COUNT      = 9
        }

        static construct {
            color_offset = Graphene.Vec4();
            color_matrix = Graphene.Matrix();

            color_matrix.init_from_float({
                0, 0, 0, 0,
                0, 0, 0, 0,
                0, 0, 0, 0,
                0, 0, 0, 1
            });
        }

        construct {
            text_repr = TextRepr.EMPTY;
            instance = this;
            name = "query-container";
            overflow = Gtk.Overflow.HIDDEN;
            css_classes = {"query-empty"};

            layouts = new Pango.Layout[TextRepr.COUNT];
            for (int i = 0; i < TextRepr.COUNT; i++) {
                layouts[i] = create_pango_layout(null);
                layouts[i].set_ellipsize(Pango.EllipsizeMode.MIDDLE);
                layouts[i].set_single_paragraph_mode(true);
            }
            layouts[TextRepr.EMPTY].set_markup(TYPE_TO_SEARCH, -1);
            layouts[TextRepr.FALLBACK_PLG].set_markup(SELECT_PLUGIN, -1);

            DragAndDropHandler.setup(this, match_finder);
            if (AppSettings.get_default().layershell.enabled) {
                setup_click_controller(this, AppSettings.get_default().layershell);
                setup_drag_controller(this, AppSettings.get_default().layershell, should_drag);
            }

            cursor_w = new CursorWidget();
            cursor_w.set_parent(this);
        }

        private bool should_drag(double x, double y) {
            return !(x >= thumbnail_location() && State.sf == SearchingFor.ACTIONS);
        }

        private unowned Match? match_finder(double x, double y) {
            if (State.sf != SearchingFor.ACTIONS) return null;
            if (x < thumbnail_location()) return null;
            return State.selected_source();
        }

        private int thumbnail_location() {
            return get_width() - get_height();
        }

        private static string format_place_holder(string text) {
            return GLib.Markup.escape_text(text);
        }

        internal static void adjust_label_for_query(string text, int cursor_position) {
            int has_text = text != "" ? 1 : 0;
            text_repr = (TextRepr)((int)State.sf * 2 + has_text);
            instance.set_cursor_position(cursor_position);  // Update cursor_w position when text changes
            switch (text_repr) {
                case TextRepr.FALLBACK_PLG:
                    break;
                case TextRepr.FALLBACK_SRC:
                    unowned Match? sb = State.selected_plugin();
                    if (sb != null) {
                        string formatted = format_place_holder(sb.get_title());
                        layouts[text_repr].set_markup(formatted, formatted.data.length);
                    } else {
                        text_repr = TextRepr.EMPTY;
                    }
                    break;
                case TextRepr.FALLBACK_ACT:
                    string title = State.selected_source().get_title();
                    string formatted = format_place_holder(title);
                    layouts[text_repr].set_markup(formatted, formatted.data.length);
                    break;
                case TextRepr.FALLBACK_TAR:
                    string title = State.selected_action().get_title();
                    string formatted = format_place_holder(title);
                    layouts[text_repr].set_markup(formatted, formatted.data.length);
                    break;
                case TextRepr.PLG:
                case TextRepr.SRC:
                case TextRepr.ACT:
                case TextRepr.TAR:
                    string formatted = GLib.Markup.escape_text(text);
                    layouts[text_repr].set_markup(formatted, formatted.data.length);
                    break;
                case TextRepr.COUNT:
                case TextRepr.EMPTY:
                    break;
            }

            if (text_repr % 2 == 0) {
                instance.add_css_class("query-empty");
            } else {
                instance.remove_css_class("query-empty");
            }

            instance.queue_draw();
        }

        public void set_cursor_position(int position) {
            cursor_position = position;
            cursor_w.remove_css_class("blinking");
        }

        internal override void measure(Gtk.Orientation orientation, int for_size, out int minimum, out int natural, out int minimum_baseline, out int natural_baseline) {
            minimum_baseline = natural_baseline = -1;
            if (orientation == Gtk.Orientation.VERTICAL) {
                layouts[TextRepr.EMPTY].get_size(null, out selected_row_height);
                selected_row_height /= Pango.SCALE;
                minimum = natural = selected_row_height;
            } else {
                minimum = natural = 0;
            }
        }

        internal override void size_allocate(int width, int height, int baseline) {
            for (int i = TextRepr.FALLBACK_PLG; i < TextRepr.COUNT; i++) {
                int glass_width = i == TextRepr.FALLBACK_SRC ? height : 0;
                int draggable_width = (i >= TextRepr.FALLBACK_ACT) ? height : 0;
                int label_width = width - glass_width - draggable_width;
                int layout_width = label_width * Pango.SCALE;
                layouts[i].set_width(layout_width);
            }
            int cursor_width;
            cursor_w.measure(Gtk.Orientation.HORIZONTAL, 0, null, out cursor_width, null, null);
            cursor_w.allocate(cursor_width, selected_row_height, baseline, null);
        }

        private void snapshot_glass(Gtk.Snapshot snapshot, Gdk.RGBA color, int height) {
            if (text_repr != TextRepr.EMPTY) return;

            color_offset.init(color.red, color.green, color.blue, 0);
            snapshot.push_color_matrix(color_matrix, color_offset);
            unowned Gdk.Paintable glass = IconCacheService.get_paintable_for_icon_name("magnifying-glass-symbolic", height, get_scale_factor());
            glass.snapshot(snapshot, height, height);
            snapshot.pop();
            snapshot.translate({ height, 0 });
        }

        private void snapshot_drag_image(Gtk.Snapshot snapshot, int height) {
            if (State.sf < SearchingFor.ACTIONS) return;
            unowned Match? m = State.selected_source();
            if (m == null) return;
            unowned Gdk.Paintable match_icon = IconCacheService.get_paintable_for_icon_name(m.get_icon_name(), height, get_scale_factor());
            snapshot.save();
            snapshot.translate({ thumbnail_location(), 0 });
            match_icon.snapshot(snapshot, height, height);
            snapshot.restore();
        }

        private void snapshot_cursor(Gtk.Snapshot snapshot, Gdk.RGBA color) {
            Pango.Rectangle strong_pos;
            layouts[text_repr].get_cursor_pos(cursor_position, out strong_pos, null);
            // Convert Pango units to pixels
            float cursor_x = (float)strong_pos.x / Pango.SCALE;
            float cursor_y = (float)strong_pos.y / Pango.SCALE;
            snapshot.translate({cursor_x, cursor_y});
            snapshot_child(cursor_w, snapshot);
        }

        protected override void snapshot(Gtk.Snapshot snapshot) {
            var color = get_color();
            // we need to do it this way. instead of using the alpha of the font,
            // we set the alpha of the font to 1.0f and use the alpha of the font as the
            // opacity of the whole snapshot. this is because otherwise we cannot ensure
            // the magnifying glass will have the same color as that of the text.
            float font_alpha = color.alpha;
            color.alpha = 1.0f;
            int height = get_height();

            snapshot_drag_image(snapshot, height);

            snapshot.push_opacity(font_alpha);

            snapshot_glass(snapshot, color, height);
            snapshot.translate({ 0, (height - selected_row_height) / 2 });
            snapshot.append_layout(layouts[text_repr], color);
            snapshot_cursor(snapshot, color);

            snapshot.pop();

            cursor_w.add_css_class("blinking");
        }
    }
}
