namespace BobLauncher {
    internal class MatchRow : Gtk.Widget {
        private const string SHORTCUT_CSS = "shortcut";
        private const string HORIZONTAL_CSS = "horizontal";
        private const string VERTICAL_CSS = "vertical";

        private static Gdk.Cursor pointer;
        private static unowned GLib.Settings settings;
        private static unowned AppSettings.UI ui_settings;

        internal int abs_index;
        internal int event_id;

        private MatchRowLabel description;
        private MatchRowLabel title;
        private RowNumber selected_row;
        private Gtk.Label shortcut;
        private Gtk.Widget? icon_widget;

        private bool was_interesting;
        private Gtk.Orientation orientation;
        private int icon_size;
        private int match_row_height;
        private int selected_row_width;
        private int shortcut_width;

        private string? title_string;
        private string? description_string;
        private string? icon_name;
        private Highlight.Positions? title_positions = null;
        private Highlight.Positions? description_positions = null;
        private Highlight.Style highlight_style = Highlight.Style.COLOR;

        private unowned Description rich_description;

        static construct {
            set_css_name("match-row");
            ui_settings = AppSettings.get_default().ui;
            settings = ui_settings.settings;
            pointer = new Gdk.Cursor.from_name("pointer", null);
            tooltip_wrapper = new TooltipWrapper();
        }

        internal MatchRow(int abs_index) {
            Object(has_tooltip: true, overflow: Gtk.Overflow.HIDDEN);

            this.abs_index = abs_index;

            title = new MatchRowLabel({"title"});
            title.set_parent(this);

            description = new MatchRowLabel({"description"});
            description.set_parent(this);

            selected_row = new RowNumber(abs_index);
            selected_row.set_parent(this);

            shortcut = new Gtk.Label("");
            shortcut.add_css_class(SHORTCUT_CSS);

            shortcut.set_parent(this);

            // Load highlight style from settings
            var style_string = settings.get_string("highlight-style");
            highlight_style = parse_highlight_style(style_string);

            settings.changed["shortcut-indicator"].connect(update_ui);
            settings.changed["match-description-next-to-title"].connect(update_ui);
            settings.changed["match-icon-size"].connect(update_ui);
            settings.changed["highlight-style"].connect(on_highlight_style_changed);
            ui_settings.accent_color_changed.connect_after(update_styling);
            query_tooltip.connect(on_query_tooltip);

            DragAndDropHandler.setup(this, match_finder);
            update_ui();
        }

        private Highlight.Style parse_highlight_style(string styles) {
            var mystyles = styles.split("|");
            Highlight.Style style = 0;
            foreach (var s in mystyles) {
                switch (s) {
                    case "underline":
                        style |= Highlight.Style.UNDERLINE;
                        break;
                    case "bold":
                        style |= Highlight.Style.BOLD;
                        break;
                    case "background":
                        style |= Highlight.Style.BACKGROUND;
                        break;
                    case "color":
                        style |= Highlight.Style.COLOR;
                        break;
                    default:
                        warning("unrecognized color style: %s", s);
                        break;
                }
            }
            return style;
        }

        private void on_highlight_style_changed() {
            var style_string = settings.get_string("highlight-style");
            highlight_style = parse_highlight_style(style_string);
            update_styling();
        }

        protected override void dispose() {
            // Clean up cached positions
            if (title_positions != null) {
                title_positions = null; // Will call free automatically
            }
            if (description_positions != null) {
                description_positions = null;
            }
            Utils.iterate_children(get_first_child(), (child) => child.unparent());
            base.dispose();
        }

        private class TooltipWrapper : Gtk.Widget {
            public int max_width { get; set; }
            public int max_height { get; set; }
            private unowned Gtk.Widget? child;
            private Gtk.SizeRequestMode request_mode = Gtk.SizeRequestMode.CONSTANT_SIZE;

            static construct {
                set_css_name("box");
            }

            internal override Gtk.SizeRequestMode get_request_mode() {
                return request_mode;
            }

            construct {
                var settings = new GLib.Settings(BOB_LAUNCHER_APP_ID + ".ui");
                settings.bind("tooltip-max-height", this, "max_height", SettingsBindFlags.GET);
                settings.bind("tooltip-max-width", this, "max_width", SettingsBindFlags.GET);
            }

            protected override void measure(Gtk.Orientation orientation,
                                          int for_size,
                                          out int minimum,
                                          out int natural,
                                          out int minimum_baseline,
                                          out int natural_baseline) {
                minimum = natural = minimum_baseline = natural_baseline = -1;

                if (child == null) return;

                int dim = orientation == Gtk.Orientation.VERTICAL ? max_height : max_width;

                if (for_size > -1) {
                    dim = int.min(for_size, dim);
                }

                child.measure(orientation, dim,
                             out minimum, out natural,
                             out minimum_baseline, out natural_baseline);

                if (orientation == Gtk.Orientation.VERTICAL) {
                    minimum = int.min(max_height, minimum);
                    natural = int.min(max_height, natural);
                } else {
                    minimum = int.min(max_width, minimum);
                    natural = int.min(max_width, natural);
                }
            }

            protected override void size_allocate(int width, int height, int baseline) {
                if (child == null) return;
                child.allocate(width, height, baseline, null);
            }

            protected override void snapshot(Gtk.Snapshot snapshot) {
                if (child == null) return;
                snapshot_child(child, snapshot);
            }

            public void change_widget(Gtk.Widget new_widget) {
                if (child != null) {
                    child.unparent();
                }
                new_widget.set_parent(this);
                child = new_widget;

                int child_width, child_height;
                child.measure(Gtk.Orientation.HORIZONTAL, -1,
                             null, out child_width,
                             null, null);

                child.measure(Gtk.Orientation.VERTICAL, -1,
                             null, out child_height,
                             null, null);

                if (child_height > child_width) {
                    request_mode = Gtk.SizeRequestMode.WIDTH_FOR_HEIGHT;
                } else {
                    request_mode = Gtk.SizeRequestMode.HEIGHT_FOR_WIDTH;
                }
            }
        }

        private static TooltipWrapper tooltip_wrapper;

        private bool on_query_tooltip(int x, int y, bool keyboard_tooltip, Gtk.Tooltip tooltip) {
            unowned Gtk.Widget? picked_widget = pick(x, y, Gtk.PickFlags.DEFAULT);
            if (picked_widget == null) return false;
            bool is_interesting = picked_widget.has_css_class("clickable");

            if (is_interesting != was_interesting) {
                set_cursor(is_interesting ? pointer : null);
                was_interesting = is_interesting;
            }

            unowned Match? m = State.current_provider().get_match_at(abs_index);
            if (m == null) return false;
            unowned Gtk.Widget? tooltip_w = m.get_tooltip();
            if (tooltip_w == null) return false;
            tooltip_wrapper.change_widget(tooltip_w);
            tooltip.set_custom(tooltip_wrapper);
            return true;
        }

        private unowned Match? match_finder(double x, double y) {
            if (State.sf != SearchingFor.SOURCES) return null;
            return State.providers[SearchingFor.SOURCES].get_match_at(abs_index);
        }

        private void update_ui() {
            match_row_height = 0;

            if (settings.get_boolean("match-description-next-to-title")) {
                orientation = Gtk.Orientation.HORIZONTAL;
                add_css_class(HORIZONTAL_CSS);
                remove_css_class(VERTICAL_CSS);
            } else {
                orientation = Gtk.Orientation.VERTICAL;
                remove_css_class(HORIZONTAL_CSS);
                add_css_class(VERTICAL_CSS);
            }

            icon_size = settings.get_int("match-icon-size");
            shortcut.set_markup(settings.get_string("shortcut-indicator"));

            queue_resize();
        }

        internal void update_match(Levensteihn.StringInfo si) {
            unowned Match? m = State.current_provider().get_match_at(abs_index);
            if (m == null) return;

            title_string = m.get_title();
            title_positions = new Highlight.Positions(si, title_string);

            unowned Gdk.RGBA accent_color = Highlight.get_accent_color();
            Pango.AttrList title_attrs = Highlight.apply_style(title_positions,
                                                               highlight_style,
                                                               accent_color);
            title.set_text(title_string, title_attrs);

            rich_description = (m is IRichDescription) ?
                ((IRichDescription)m).get_rich_description(si) : null;

            if (rich_description == null) {
                description_string = m.get_description();
                description_positions = new Highlight.Positions(si, description_string);

                Pango.AttrList desc_attrs = Highlight.apply_style(description_positions,
                                                                  highlight_style,
                                                                  accent_color);
                description.set_text(description_string, desc_attrs);
            } else {
                description.set_description(rich_description);
                description.add_css_class("description");

                description_string = null;
                description_positions = null;
            }

            if (icon_widget != null) {
                icon_widget.unparent();
                icon_widget = null;
            }

            if (m is IRichIcon) {
                icon_widget = ((IRichIcon)m).get_rich_icon();
                icon_widget.set_parent(this);
            } else {
                icon_name = m.get_icon_name();
            }

            queue_draw();
        }


        private void update_styling() {
            if (title_string != null && title_positions != null) {
                unowned Gdk.RGBA accent_color = Highlight.get_accent_color();
                Pango.AttrList title_attrs = Highlight.apply_style(title_positions,
                                                                   highlight_style,
                                                                   accent_color);
                title.set_text(title_string, title_attrs);
            }

            if (description_string != null && description_positions != null) {
                unowned Gdk.RGBA accent_color = Highlight.get_accent_color();
                Pango.AttrList desc_attrs = Highlight.apply_style(description_positions,
                                                                  highlight_style,
                                                                  accent_color);
                description.set_text(description_string, desc_attrs);
            }

            queue_draw();
        }

        internal void update(Levensteihn.StringInfo si, int new_row, int new_abs_index, bool row_selected, int new_event) {
            int prev_abs_index = Atomics.exchange(ref abs_index, new_abs_index);
            int prev_event = Atomics.exchange(ref event_id, new_event);

            if (prev_event != new_event || prev_abs_index != new_abs_index) {
                update_match(si);
            }

            Gtk.StateFlags flag = row_selected ? Gtk.StateFlags.SELECTED : Gtk.StateFlags.NORMAL;
            this.set_state_flags(flag, true);
            selected_row.update_row_num(new_row);
        }

        internal override Gtk.SizeRequestMode get_request_mode() {
            return Gtk.SizeRequestMode.CONSTANT_SIZE;
        }

        private int title_width;
        private int desc_width;
        private int title_height = 0;
        private int desc_height = 0;
        private int title_nat_baseline = 0;
        private int desc_nat_baseline = 0;

        protected override void measure(Gtk.Orientation o, int for_size, out int minimum, out int natural, out int minimum_baseline, out int natural_baseline) {
            if (o == Gtk.Orientation.VERTICAL) {
                int shortcut_nat, selected_row_nat;
                if (match_row_height == 0) {
                    title.measure(Gtk.Orientation.VERTICAL, -1, null, out title_height, null, out title_nat_baseline);
                    description.measure(Gtk.Orientation.VERTICAL, -1, null, out desc_height, null, out desc_nat_baseline);
                    shortcut.measure(Gtk.Orientation.VERTICAL, -1, null, out shortcut_nat, null, null);
                    selected_row.measure(Gtk.Orientation.VERTICAL, -1, null, out selected_row_nat, null, null);

                    if (orientation == Gtk.Orientation.VERTICAL) {
                        match_row_height = int.max(int.max(int.max(icon_size, title_height + desc_height), shortcut_nat), selected_row_nat);
                    } else {
                        match_row_height = int.max(int.max(int.max(int.max(icon_size, title_height), shortcut_nat), selected_row_nat), desc_height);
                    }
                }

                minimum_baseline = natural_baseline = int.max(desc_nat_baseline, title_nat_baseline);
                minimum = natural = match_row_height;
            } else {
                natural = minimum = 0;
                minimum_baseline = natural_baseline = -1;
                selected_row.measure(Gtk.Orientation.HORIZONTAL, -1, null, out selected_row_width, null, null);
                shortcut.measure(Gtk.Orientation.HORIZONTAL, -1, null, out shortcut_width, null, null);
                if (orientation == Gtk.Orientation.HORIZONTAL) {
                    description.measure(Gtk.Orientation.HORIZONTAL, -1, null, out desc_width, null, null);
                    title.measure(Gtk.Orientation.HORIZONTAL, -1, null, out title_width, null, null);
                }
            }
        }

        protected override void size_allocate(int width, int height, int baseline) {
            float fheight = (float)height;
            float fhalf = fheight / 2.0f;

            Gsk.Transform transform = new Gsk.Transform();


            selected_row.allocate(selected_row_width, height, baseline, transform.translate({ width - selected_row_width, 0 }));
            shortcut.allocate(shortcut_width, height, baseline, transform.translate({ width - shortcut_width - selected_row_width, 0 }));

            if (this.orientation == Gtk.Orientation.VERTICAL) {
                int text_width = width - icon_size - shortcut_width - selected_row_width;
                int leftover_height = height - title_height - desc_height;
                float middle_adjustment = leftover_height / 4.0f;
                title.allocate(text_width, title_height, title_nat_baseline, transform.translate({ icon_size, middle_adjustment }));
                description.allocate(text_width, desc_height, desc_nat_baseline, transform.translate({ icon_size, title_height + middle_adjustment + middle_adjustment }));
            } else {
                float ftitle_height = (float)title_height;
                float title_label_shift = fhalf - (ftitle_height / 2.0f);
                float desc_label_shift = ((float)(height - desc_height)) / 2.0f;
                int _title_width = int.min(width - icon_size - shortcut_width - selected_row_width, title_width);
                title.allocate(_title_width, title_height, title_nat_baseline, transform.translate({ icon_size, title_label_shift }));
                int _desc_width = int.min(width - icon_size - shortcut_width - selected_row_width, desc_width);
                description.allocate(_desc_width, desc_height, desc_nat_baseline, transform.translate({ icon_size + _title_width, desc_label_shift }));
            }

            if (icon_widget != null) {
                transform = new Gsk.Transform().translate({0, ((float)(get_height() - icon_size)) / 2.0f});
                icon_widget.allocate(icon_size, icon_size, -1, transform);
            }
        }

        protected override void snapshot(Gtk.Snapshot snapshot) {
            snapshot_child(title, snapshot);
            snapshot_child(description, snapshot);
            snapshot_child(selected_row, snapshot);
            snapshot_child(shortcut, snapshot);

            if (icon_widget != null) {
                snapshot_child(icon_widget, snapshot);
            } else {
                snapshot.translate({0, ((float)(get_height() - icon_size)) / 2.0f});
                unowned Gdk.Paintable p = IconCacheService.get_paintable_for_icon_name(icon_name, icon_size, scale_factor);
                p.snapshot(snapshot, icon_size, icon_size);
            }
        }
    }
}
