namespace BobLauncher {
    internal class MatchRowLabel : Gtk.Widget {
        private const int FADE_WIDTH = 24;
        private double scroll_position = 0;
        private int children_Width;
        private int current_widget_index;
        private double total_overhang = 0.0;
        private double last_delta = 0.0;

        private int visible_images;
        private int visible_labels;

        private GenericArray<TextImage> images;
        private GenericArray<Gtk.Label> labels;

        private int[] widget_lengths;

        Graphene.Rect bounds;
        Graphene.Point start;
        Gsk.ColorStop[] stops;

        static construct {
            set_css_name("match-row-label");
        }

        internal MatchRowLabel(string[] css_classes) {
            Object(css_classes: css_classes);
        }

        private const Gtk.EventControllerScrollFlags flags =
            Gtk.EventControllerScrollFlags.HORIZONTAL
            | Gtk.EventControllerScrollFlags.KINETIC;

        construct {
            // valign = Gtk.Align.BASELINE_CENTER;
            halign = Gtk.Align.START;
            hexpand = true;
            overflow = Gtk.Overflow.HIDDEN;

            start = Graphene.Point() { x = 0, y = 0 };
            stops = new Gsk.ColorStop[4];

            var scroll_controller = new Gtk.EventControllerScroll(flags);
            add_controller(scroll_controller);
            scroll_controller.decelerate.connect(on_decelerate);
            scroll_controller.scroll.connect(on_scroll);

            images = new GenericArray<TextImage>();
            labels = new GenericArray<Gtk.Label>();
            widget_lengths = new int[]{};
        }

        private bool on_scroll(double x, double y) {
            total_overhang = 0;
            last_delta = x;
            update_scroll_position(x);
            return true;
        }

        private void on_decelerate(double x, double y) {
            total_overhang = x;
            queue_draw();
        }

        private void update_scroll_position(double dx) {
            scroll_position = double.max(0, scroll_position + dx);
            var our_width = this.get_width();
            if (scroll_position > children_Width - our_width) {
                scroll_position = double.min(scroll_position, children_Width - our_width);
                total_overhang = 0;
            } else if (children_Width > our_width) {
                scroll_position = double.min(scroll_position, children_Width - our_width);
            } else {
                total_overhang = 0;
                scroll_position = 0;
            }
            this.queue_allocate();
        }

        internal void reset() {
            current_widget_index = 0;
            scroll_position = 0;
            total_overhang = 0;

            visible_images = 0;
            visible_labels = 0;
        }

        private unowned Gtk.Widget get_or_create_widget(FragmentType type) {
            unowned Gtk.Widget widget;

            if (type == FragmentType.IMAGE) {
                if (visible_images < images.length) {
                    widget = images[visible_images];
                    widget.set_data("fragment", null);
                } else {
                    var icon = new TextImage();
                    images.add(icon);
                    widget_lengths+=0;
                    widget = icon;
                }
                visible_images++;
            } else {
                if (visible_labels < labels.length) {
                    widget = labels[visible_labels];
                    widget.set_data("fragment", null);
                } else {
                    var label = new Gtk.Label("") {
                        xalign = 0.0f,
                        css_classes = { "fragment" },
                        single_line_mode = true,
                        hexpand = true,
                        valign = Gtk.Align.BASELINE_CENTER,
                        vexpand = true,
                        overflow = Gtk.Overflow.VISIBLE,
                        use_markup = true,
                    };
                    labels.add(label);
                    widget_lengths+=0;
                    widget = label;
                }
                visible_labels++;
            }

            unowned Gtk.Widget? sibling = get_first_child();
            uint count = 0;
            while (sibling != null && count++ < current_widget_index) {
                sibling = sibling.get_next_sibling();
            }
            widget.insert_before(this, sibling);
            current_widget_index++;

            return widget;
        }

        internal void set_markup(string text) {
            reset();
            Gtk.Label label = ((Gtk.Label)get_or_create_widget(FragmentType.TEXT));
            label.css_classes = { };
            label.set_markup(text);
        }

        internal void set_children(GenericArray<Description> descs) {
            reset();

            foreach (var frag in descs) {
                switch (frag.fragment_type) {
                    case FragmentType.IMAGE:
                        unowned TextImage icon = (TextImage)get_or_create_widget(FragmentType.IMAGE);
                        icon.update(frag.text, (owned)frag.fragment_func);
                        break;
                    case FragmentType.TEXT:
                        unowned Gtk.Label label = (Gtk.Label)get_or_create_widget(FragmentType.TEXT);
                        if (frag.text != label.label) {
                            label.set_markup(frag.text);
                        }

                        if (frag.fragment_func != null) {
                            label.set_data("fragment", new FragmentAction((owned)frag.fragment_func));
                        }
                        break;
                    default:
                        break;
                }
            }
        }

        internal override void snapshot(Gtk.Snapshot snapshot) {
            int width = get_width();
            bool need_left_mask = scroll_position > 0;
            bool need_right_mask = scroll_position < (children_Width - width);

            if (!need_left_mask && !need_right_mask) {
                unowned Gtk.Widget? sibling = get_first_child();
                uint count = 0;
                while (sibling != null && count++ < current_widget_index) {
                    snapshot_child(sibling, snapshot);
                    sibling = sibling.get_next_sibling();
                }

                return;
            }

            if (need_left_mask) {
                float progress = float.min((float)scroll_position.abs(), FADE_WIDTH);
                float t = progress / FADE_WIDTH;
                float eased = FADE_WIDTH * (1 - (1 - t) * (1 - t) * (1 - t));
                stops[0] = { 0.0f, { 0, 0, 0, 0 } };
                stops[1] = { eased / width, { 0, 0, 0, 1 } };
            } else {
                stops[0] = { 0.0f, { 0, 0, 0, 1 } };
                stops[1] = { 0.0f, { 0, 0, 0, 1 } };
            }

            if (need_right_mask) {
                float progress = float.min((float)(children_Width - width - scroll_position), FADE_WIDTH);
                float t = progress / FADE_WIDTH;
                float eased = FADE_WIDTH * (1 - (1 - t) * (1 - t) * (1 - t));
                stops[2] = { 1.0f - (eased / width), { 0, 0, 0, 1 } };
                stops[3] = { 1.0f, { 0, 0, 0, 0 } };
            } else {
                stops[2] = { 1.0f, { 0, 0, 0, 1 } };
                stops[3] = { 1.0f, { 0, 0, 0, 1 } };
            }

            this.compute_bounds(this, out bounds);
            snapshot.push_mask(Gsk.MaskMode.ALPHA);
            snapshot.append_linear_gradient(bounds, start, {width, 0}, stops);
            snapshot.pop();

            unowned Gtk.Widget? sibling = get_first_child();
            uint count = 0;
            while (sibling != null && count++ < current_widget_index) {
                snapshot_child(sibling, snapshot);
                sibling = sibling.get_next_sibling();
            }

            snapshot.pop();

            double momentum = total_overhang * 0.02;  // Consume 2% each time
            total_overhang -= momentum;
            if (total_overhang.abs() > 0.15) {
                update_scroll_position(momentum);
            } else {
                total_overhang = 0.0;
            }
        }

        internal override void size_allocate(int width, int height, int baseline) {
            scroll_position = scroll_position.clamp(0, children_Width - width);
            var transform = new Gsk.Transform().translate({ -(float)scroll_position, 0 });

            unowned Gtk.Widget? sibling = get_first_child();
            uint count = 0;
            while (sibling != null && count++ < current_widget_index) {
                int widget_width = widget_lengths[count-1];
                sibling.allocate(widget_width, height, baseline, transform);
                transform = transform.translate({widget_width, 0});
                sibling = sibling.get_next_sibling();
            }
        }

        internal override void measure(Gtk.Orientation orientation,
                                   int for_size,
                                   out int minimum,
                                   out int natural,
                                   out int minimum_baseline,
                                   out int natural_baseline) {
            natural = minimum = 0;
            natural_baseline = minimum_baseline = -1;
            if (orientation == Gtk.Orientation.VERTICAL) {
                unowned Gtk.Widget? sibling = get_first_child();
                uint count = 0;
                while (sibling != null && count++ < current_widget_index) {
                    int child_height, child_nat_baseline, child_min_baseline;
                    sibling.measure(Gtk.Orientation.VERTICAL, -1, null, out child_height, out child_min_baseline, out child_nat_baseline);
                    natural = int.max(child_height, natural);
                    natural_baseline = int.max(child_nat_baseline, natural_baseline);
                    minimum_baseline = int.max(child_min_baseline, minimum_baseline);
                    sibling = sibling.get_next_sibling();
                }
            } else {
                children_Width = 0;
                unowned Gtk.Widget? sibling = get_first_child();
                uint count = 0;
                while (sibling != null && count++ < current_widget_index) {
                    int child_width;
                    sibling.measure(Gtk.Orientation.HORIZONTAL, -1, null, out child_width, null, null);
                    widget_lengths[count-1] = child_width;
                    natural += child_width;
                    children_Width += child_width;
                    sibling = sibling.get_next_sibling();
                }
            }
            minimum = natural;
        }

        internal override Gtk.SizeRequestMode get_request_mode() {
            return Gtk.SizeRequestMode.CONSTANT_SIZE;
        }
    }
}
