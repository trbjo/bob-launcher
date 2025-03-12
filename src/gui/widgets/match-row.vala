namespace BobLauncher {
    internal class MatchRow : Gtk.Widget {
        private static Gdk.Cursor pointer;
        private static unowned GLib.Settings settings;
        private static unowned AppSettings.UI ui_settings;

        internal int abs_index;
        internal int event_id;

        private MatchRowLabel description;
        private MatchRowLabel title;
        private RowNumber selected_row;
        private Gtk.Label shortcut;

        private bool was_interesting;
        private Gtk.Orientation orientation;
        private int icon_size;
        private int match_row_height;
        private int selected_row_width;
        private int shortcut_width;

        private string? title_string;
        private string? description_string;
        private string? icon_name;

        private unowned GenericArray<Description>? rich_descriptions;

        static construct {
            set_css_name("match-row");
            ui_settings = AppSettings.get_default().ui;
            settings = ui_settings.settings;
            pointer = new Gdk.Cursor.from_name("pointer", null);
        }

        internal MatchRow(int abs_index) {
            Object(has_tooltip: true, overflow: Gtk.Overflow.HIDDEN);
            orientation = settings.get_boolean("match-description-next-to-title") ? Gtk.Orientation.HORIZONTAL : Gtk.Orientation.VERTICAL;

            this.abs_index = abs_index;

            title = new MatchRowLabel({"title"});
            title.set_parent(this);

            description = new MatchRowLabel({"description"});
            description.set_parent(this);

            selected_row = new RowNumber(abs_index);
            selected_row.set_parent(this);

            shortcut = new Gtk.Label("") { css_classes = {"shortcut"} };
            shortcut.set_parent(this);

            update_shortcut_label();
            update_icon_size();

            settings.changed["shortcut-indicator"].connect(update_shortcut_label);
            settings.changed["match-description-next-to-title"].connect(update_text_orientation);
            settings.changed["match-icon-size"].connect(update_icon_size);
            ui_settings.accent_color_changed.connect_after(update_match);
            query_tooltip.connect(on_query_tooltip);

            DragAndDropHandler.setup(this, match_finder);
        }

        protected override void dispose() {
            Utils.iterate_children(get_first_child(), (child) => child.unparent());
            base.dispose();
        }

        private bool on_query_tooltip(int x, int y, bool keyboard_tooltip, Gtk.Tooltip tooltip) {
            unowned Gtk.Widget? picked_widget = pick(x, y, Gtk.PickFlags.DEFAULT);
            bool is_interesting = picked_widget.get_data<Object>("fragment") != null;

            if (is_interesting != was_interesting) {
                set_cursor(is_interesting ? pointer : null);
                was_interesting = is_interesting;
            }

            unowned Match? m = State.current_provider().get_match_at(abs_index);
            if (m == null) return false;
            Gtk.Widget? tooltip_w = m.get_tooltip();
            if (tooltip_w == null) return false;
            tooltip.set_custom(tooltip_w);
            return true;
        }

        private Match? match_finder(double x, double y) {
            if (State.sf != SearchingFor.SOURCES) return null;
            return State.providers[SearchingFor.SOURCES].get_match_at(abs_index);
        }

        private void update_icon_size() {
            icon_size = settings.get_int("match-icon-size");
            match_row_height = 0;
            queue_resize();
        }

        private void update_text_orientation() {
            orientation = settings.get_boolean("match-description-next-to-title") ? Gtk.Orientation.HORIZONTAL : Gtk.Orientation.VERTICAL;
            match_row_height = 0;
            queue_resize();
        }

        private void update_shortcut_label() {
            shortcut.set_markup(settings.get_string("shortcut-indicator"));
        }

        internal void update_match() {
            unowned Match? m = State.current_provider().get_match_at(abs_index);
            if (m == null) return;
            unowned Levensteihn.StringInfo si = State.current_provider().string_info_spaceless;

            string? highlight_color = Highlight.get_pango_accent();

            title_string = Highlight.format_highlights(m.get_title(), highlight_color, si);
            rich_descriptions = (m is IRichDescription) ? ((IRichDescription)m).get_rich_description(si) : null;
            description_string = rich_descriptions == null ? Highlight.format_highlights(m.get_description(), highlight_color, si) : null;
            icon_name = m.get_icon_name();

            title.set_markup(title_string);
            if (description_string == null) {
                description.set_children(rich_descriptions);
            } else {
                description.set_markup(description_string);
            }
            queue_draw();
        }

        internal void update(int new_row, int new_abs_index, bool row_selected, int new_event) {
            int prev_abs_index = Atomics.exchange(ref abs_index, new_abs_index);
            int prev_event = Atomics.exchange(ref event_id, new_event);

            if (prev_event != new_event || prev_abs_index != new_abs_index) {
                update_match();
            }

            Gtk.StateFlags flag = row_selected ? Gtk.StateFlags.SELECTED : Gtk.StateFlags.NORMAL;
            this.set_state_flags(flag, true);
            selected_row.update_row_num(new_row);
        }

        internal override Gtk.SizeRequestMode get_request_mode() {
            return Gtk.SizeRequestMode.CONSTANT_SIZE;
        }

        protected override void measure(Gtk.Orientation orientation, int for_size, out int minimum, out int natural, out int minimum_baseline, out int natural_baseline) {
            minimum_baseline = natural_baseline = -1;
            if (orientation == Gtk.Orientation.VERTICAL) {
                if (match_row_height == 0) {
                    int text_nat, desc_nat, shortcut_nat, selected_row_nat;
                    title.measure(Gtk.Orientation.VERTICAL, -1, null, out text_nat, null, null);
                    description.measure(Gtk.Orientation.VERTICAL, -1, null, out desc_nat, null, null);
                    shortcut.measure(Gtk.Orientation.VERTICAL, -1, null, out shortcut_nat, null, null);
                    selected_row.measure(Gtk.Orientation.VERTICAL, -1, null, out selected_row_nat, null, null);
                    if (orientation == Gtk.Orientation.VERTICAL) {
                        match_row_height = int.max(int.max(int.max(icon_size, text_nat + desc_nat), shortcut_nat), selected_row_nat);
                    } else {
                        match_row_height = int.max(int.max(int.max(int.max(icon_size, text_nat), shortcut_nat), selected_row_nat), desc_nat);
                    }
                }
                minimum = natural = match_row_height;
            } else {
                natural = minimum = 0;
                selected_row.measure(Gtk.Orientation.HORIZONTAL, -1, null, out selected_row_width, null, null);
                shortcut.measure(Gtk.Orientation.HORIZONTAL, -1, null, out shortcut_width, null, null);
            }
        }

        protected override void size_allocate(int width, int height, int baseline) {
            Gsk.Transform transform = new Gsk.Transform();

            selected_row.allocate(selected_row_width, height, baseline, transform.translate({ width - selected_row_width, 0 }));
            shortcut.allocate(shortcut_width, height, baseline, transform.translate({ width - shortcut_width - selected_row_width, 0 }));

            if (orientation == Gtk.Orientation.VERTICAL) {
                int text_width = width - icon_size - shortcut_width - selected_row_width;
                title.allocate(text_width, height / 2, baseline, transform.translate({ icon_size, 0 }));
                description.allocate(text_width, height / 2, baseline, transform.translate({ icon_size, height / 2 }));
            } else {
                int title_nat;

                title.measure(Gtk.Orientation.HORIZONTAL, -1, null, out title_nat, null, null);
                title_nat = int.min(width - icon_size - shortcut_width - selected_row_width, title_nat);
                title.allocate(title_nat, height, baseline, transform.translate({ icon_size, 0 }));
                int desc_width = int.max(0, width - title_nat - icon_size - shortcut_width - selected_row_width);
                description.allocate(desc_width, height, baseline, transform.translate({ icon_size + title_nat, 0 }));
            }
        }

        protected override void snapshot(Gtk.Snapshot snapshot) {
            snapshot_child(title, snapshot);
            snapshot_child(description, snapshot);
            snapshot_child(selected_row, snapshot);
            snapshot_child(shortcut, snapshot);
            snapshot.translate({0, (get_height() - icon_size) / 2});
            unowned Gdk.Paintable p = IconCacheService.get_paintable_for_icon_name(icon_name, icon_size, scale_factor);
            p.snapshot(snapshot, icon_size, icon_size);
        }
    }
}
