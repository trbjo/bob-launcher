namespace BobLauncher {
    internal class RowNumber : Gtk.Widget {
        private Pango.Layout selected_row_layout;
        private int selected_row_width = 0;
        private int selected_row_height = 0;
        private int row_num = 65536;

        static construct {
            set_css_name("row-number");
        }

        internal RowNumber(int row_num) {
            Object();

            halign = Gtk.Align.CENTER;
            css_classes = {"shortcut"};

            selected_row_layout = create_pango_layout(null);
            selected_row_layout.set_alignment(Pango.Alignment.CENTER);
            update_row_num(row_num);
        }

        internal void update_row_num(int new_row) {
            int wrapped = (new_row + 1) % 10;
            if (row_num == wrapped) return;
            row_num = wrapped;
            string formatted = "%u".printf(row_num);
            selected_row_layout.set_text(formatted, -1);
            selected_row_layout.get_size(out selected_row_width, out selected_row_height);
            selected_row_width /= Pango.SCALE;
            selected_row_height /= Pango.SCALE;
            queue_draw();
        }

        protected override void measure(Gtk.Orientation orientation, int for_size, out int minimum, out int natural, out int minimum_baseline, out int natural_baseline) {
            minimum_baseline = natural_baseline = -1;

            if (orientation == Gtk.Orientation.VERTICAL) {
                selected_row_layout.get_size(null, out selected_row_height);
                selected_row_height /= Pango.SCALE;
                minimum = natural = selected_row_height;
            } else {
                selected_row_layout.get_size(out selected_row_width, null);
                selected_row_width /= Pango.SCALE;
                natural = minimum = selected_row_width;
            }
        }


        protected override void snapshot(Gtk.Snapshot snapshot) {
            snapshot.translate({ (get_width() - selected_row_width) / 2, (get_height() - selected_row_height) / 2 });
            var color = get_color();
            snapshot.append_layout(selected_row_layout, color);
        }
    }
}
