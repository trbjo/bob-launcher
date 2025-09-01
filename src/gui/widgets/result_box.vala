namespace BobLauncher {
    internal class ResultBox : Gtk.Widget {
        internal class Separator : Gtk.Widget {
            static construct {
                set_css_name("matchrow-separator");
            }
        }

        private static int[] row_sizes;
        private static int[] sep_sizes;
        private Settings ui_settings;

        internal static Separator[] separators;
        internal static int box_size;
        internal static int visible_size;

        public static MatchRow[] row_pool;

        construct {
            name = "result-box";

            ui_settings = new Settings(BOB_LAUNCHER_APP_ID + ".ui");
            box_size = ui_settings.get_int("box-size");
            ui_settings.changed["box-size"].connect(on_box_size_change);

            initialize_slots();

            ScrollController.setup(this);
        }

        private void on_box_size_change() {
            box_size = ui_settings.get_int("box-size");
            Utils.iterate_children(get_first_child(), (child) => child.unparent());
            initialize_slots();
            MainContainer.update_layout(State.providers[State.sf], State.selected_indices[State.sf]);
            queue_resize();
        }

        private void initialize_slots() {
            row_pool = new MatchRow[box_size];
            separators = new Separator[box_size];
            row_sizes = new int[box_size];
            sep_sizes = new int[box_size];

            for (int i = 0; i < box_size; i++) {
                separators[i] = new Separator();
                separators[i].set_parent(this);
                row_pool[i] = new MatchRow(i);
                row_pool[i].set_parent(this);
            }
        }

        internal override Gtk.SizeRequestMode get_request_mode() {
            return Gtk.SizeRequestMode.CONSTANT_SIZE;
        }

        internal void update_layout(Hash.HashSet provider, int selected_index) {
            LauncherWindow.up_down_handle.visible = provider.size > 0;
            int old_visible = visible_size;
            int provider_size = int.max(0, provider.size);
            visible_size = int.min(provider_size, box_size);

            int before = (visible_size - 1) / 2;
            int start_index = int.max(0, int.min(provider_size - visible_size, selected_index - before));
            int stop_index = start_index + visible_size - 1;

            int view_tail = stop_index;
            int view_head = start_index;

            for (int i = 0; i < visible_size; i++) {
                int abs_index;
                if (provider.event_id != row_pool[i].event_id) {
                    abs_index = i + start_index;
                } else {
                    int abs = row_pool[i].abs_index;
                    if (abs < start_index) {
                        abs_index = view_tail--;
                    } else if (abs > stop_index) {
                        abs_index = view_head++;
                    } else {
                        abs_index = abs;
                    }
                }

                bool select = selected_index == abs_index;
                int rel_index = abs_index - start_index;
                row_pool[i].update(rel_index, abs_index, select, provider.event_id);
            }

            sort_row_pool(provider.event_id);

            int preceding = selected_index - start_index;
            int following = selected_index - start_index + 1;

            for (int i = 0; i < separators.length; i++) {
                Gtk.StateFlags flag = (i == preceding || i == following) ? Gtk.StateFlags.SELECTED : Gtk.StateFlags.NORMAL;
                separators[i].set_state_flags(flag, true);
            }

            if (old_visible != visible_size) {
                queue_resize();
            } else {
                queue_allocate();
            }
        }

        private static void sort_row_pool(int event_id) {
            for (int i = 1; i < row_pool.length; i++) {
                var key = row_pool[i];
                int j = i - 1;

                while (j >= 0 && should_swap(row_pool[j], key)) {
                    row_pool[j + 1] = row_pool[j];
                    j--;
                }
                row_pool[j + 1] = key;
            }
        }

        private static bool should_swap(MatchRow a, MatchRow b) {
            return a.event_id < b.event_id || (a.event_id == b.event_id && a.abs_index > b.abs_index);
        }

        internal override void measure(Gtk.Orientation orientation, int for_size, out int minimum, out int natural, out int minimum_baseline, out int natural_baseline) {
            minimum_baseline = natural_baseline = -1;
            minimum = natural = 0;

            if (orientation == Gtk.Orientation.VERTICAL) {
                for (int i = 0; i < visible_size; i++) {
                    int separator_nat;
                    separators[i].measure(Gtk.Orientation.VERTICAL, for_size, null, out separator_nat, null, null);
                    sep_sizes[i] = separator_nat;
                    natural += separator_nat;

                    int child_nat;
                    row_pool[i].measure(Gtk.Orientation.VERTICAL, for_size, null, out child_nat, null, null);
                    row_sizes[i] = child_nat;
                    natural += child_nat;
                }
            } else {
                for (int i = 0; i < visible_size; i++) {
                    int child_nat;
                    row_pool[i].measure(Gtk.Orientation.HORIZONTAL, -1, null, out child_nat, null, null);
                    natural = int.max(child_nat, natural);
                }
            }
        }

        internal override void size_allocate(int width, int height, int baseline) {
            Gsk.Transform transform = new Gsk.Transform();
            var trans = Graphene.Point.zero();

            for (int i = 0; i < visible_size; i++) {
                separators[i].allocate(width, sep_sizes[i], baseline, transform);

                trans.y = sep_sizes[i];
                transform = transform.translate(trans);

                row_pool[i].allocate(width, row_sizes[i], baseline, transform);

                trans.y = row_sizes[i];
                transform = transform.translate(trans);
            }
        }

        protected override void snapshot(Gtk.Snapshot snapshot) {
            // snapshot separators first separately to draw them in the background
            for (int i = 0; i < visible_size; i++) {
                snapshot_child(separators[i], snapshot);
            }
            for (int i = 0; i < visible_size; i++) {
                snapshot_child(row_pool[i], snapshot);
            }
        }
    }
}
