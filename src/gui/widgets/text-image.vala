namespace BobLauncher {
    internal class TextImage : Gtk.Widget {
        private string? icon_name;
        private Graphene.Matrix? color_matrix;
        private Graphene.Vec4? color_offset;

        static construct {
            set_css_name("text-image");
        }

        construct {
            overflow = Gtk.Overflow.HIDDEN;
            can_target = true;
            valign = Gtk.Align.BASELINE_FILL;
            vexpand = true;
            css_classes = { "fragment" };

            color_offset = Graphene.Vec4();
            color_matrix = Graphene.Matrix();
        }

        public void update_icon_name(string new_icon_name) {
            if (this.icon_name != new_icon_name) {
                this.icon_name = new_icon_name;
                queue_draw();
            }
        }

        internal override void snapshot(Gtk.Snapshot snapshot) {
            int shortest = int.min(get_width(), get_height());
            float shift_x = 0;
            float shift_y = 0;
            if (get_width() > get_height()) {
                shift_x = ((float)(get_width() - get_height())) / 2.0f;
            } else if (get_height() > get_width()) {
                shift_y = ((float)(get_height() - get_width())) / 2.0f;
            }
            var color = get_color();

            color_matrix.init_from_float({
                0, 0, 0, 0,
                0, 0, 0, 0,
                0, 0, 0, 0,
                0, 0, 0, color.alpha
            });

            color_offset.init(color.red, color.green, color.blue, 0);

            snapshot.push_color_matrix(color_matrix, color_offset);
            unowned Gdk.Paintable p = IconCacheService.get_paintable_for_icon_name(icon_name, shortest, scale_factor);
            snapshot.translate({shift_x, shift_y});
            p.snapshot(snapshot, shortest, shortest);
            snapshot.pop();
        }
    }


}
