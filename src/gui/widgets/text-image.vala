namespace BobLauncher {
    internal class TextImage : Gtk.Widget {
        private string? icon_name;
        private Graphene.Matrix? color_matrix;
        private Graphene.Vec4? color_offset;
        private FragmentAction? fragment_action;

        static construct {
            set_css_name("text-image");
        }

        construct {
            overflow = Gtk.Overflow.VISIBLE;
            can_target = true;
            valign = Gtk.Align.BASELINE_CENTER;
            vexpand = true;
            css_classes = { "fragment" };

            color_offset = Graphene.Vec4();
            color_matrix = Graphene.Matrix();
        }

        public void update(string new_icon_name, owned FragmentFunc? func) {
            this.icon_name = new_icon_name;
            if (func != null) {
                this.fragment_action = new FragmentAction((owned)func);
            } else {
                this.fragment_action = null;
            }
        }

        internal override void snapshot(Gtk.Snapshot snapshot) {
            int dimension = int.min(get_width(), get_height());
            var color = get_color();

            color_matrix.init_from_float({
                0, 0, 0, 0,
                0, 0, 0, 0,
                0, 0, 0, 0,
                0, 0, 0, color.alpha
            });

            color_offset.init(color.red, color.green, color.blue, 0);

            snapshot.push_color_matrix(color_matrix, color_offset);
            unowned Gdk.Paintable p = IconCacheService.get_paintable_for_icon_name(icon_name, dimension, scale_factor);
            p.snapshot(snapshot, dimension, dimension);
            snapshot.pop();
        }
    }


}
