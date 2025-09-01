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
        private int visible_containers;

        private GenericArray<TextImage> images;
        private GenericArray<Gtk.Label> labels;
        private GenericArray<Gtk.Box> containers;

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
            valign = Gtk.Align.BASELINE_FILL;
            halign = Gtk.Align.START;
            hexpand = true;
            overflow = Gtk.Overflow.VISIBLE;

            start = Graphene.Point() { x = 0, y = 0 };
            stops = new Gsk.ColorStop[4];

            var scroll_controller = new Gtk.EventControllerScroll(flags);
            add_controller(scroll_controller);
            scroll_controller.decelerate.connect(on_decelerate);
            scroll_controller.scroll.connect(on_scroll);

            images = new GenericArray<TextImage>();
            labels = new GenericArray<Gtk.Label>();
            containers = new GenericArray<Gtk.Box>();
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
            visible_containers = 0;

            for (int i = 0; i < labels.length; i++) {
                var label = labels.get(i);
                label.set_attributes(null);
                label.set_text("");
            }
        }

        private unowned Gtk.Widget get_or_create_widget(FragmentType type, Gtk.Orientation orientation = Gtk.Orientation.HORIZONTAL, int spacing = 0) {
            unowned Gtk.Widget widget;
            bool is_new_widget = false;

            switch (type) {
                case FragmentType.IMAGE:
                    if (visible_images < images.length) {
                        widget = images[visible_images];
                    } else {
                        var icon = new TextImage();
                        is_new_widget = true;
                        images.add(icon);
                        widget_lengths += 0;
                        widget = icon;
                    }
                    visible_images++;
                    break;

                case FragmentType.CONTAINER:
                    if (visible_containers < containers.length) {
                        widget = containers[visible_containers];
                        // Update container properties
                        var box = (Gtk.Box)widget;
                        box.orientation = orientation;
                        box.spacing = spacing;
                    } else {
                        var box = new Gtk.Box(orientation, spacing) {
                            overflow = Gtk.Overflow.HIDDEN,
                            hexpand = true,
                            vexpand = true,
                        };
                        is_new_widget = true;
                        containers.add(box);
                        widget_lengths += 0;
                        widget = box;
                    }
                    visible_containers++;
                    break;

                default: // TEXT
                    if (visible_labels < labels.length) {
                        widget = labels[visible_labels];
                        var label = (Gtk.Label)widget;
                        label.set_attributes(null);
                        label.set_text("");
                    } else {
                        var label = new Gtk.Label("") {
                            xalign = 0.0f,
                            single_line_mode = true,
                            hexpand = true,
                            valign = Gtk.Align.BASELINE_CENTER,
                            vexpand = true,
                            overflow = Gtk.Overflow.VISIBLE,
                        };
                        is_new_widget = true;
                        labels.add(label);
                        widget_lengths += 0;
                        widget = label;
                    }
                    visible_labels++;
                    break;
            }

            unowned Gtk.Widget? sibling = get_first_child();
            uint count = 0;
            while (sibling != null && count++ < current_widget_index) {
                sibling = sibling.get_next_sibling();
            }
            widget.insert_before(this, sibling);
            current_widget_index++;

            if (is_new_widget) {
                App.main_win.unmap.connect(() => widget.set_data("fragment", null));
            } else {
                widget.set_data("fragment", null);
            }

            widget.css_classes = { "fragment" };
            return widget;
        }

        internal void set_text(string text, Pango.AttrList? attrs) {
            reset();
            hide_all_widgets();

            Gtk.Label label = ((Gtk.Label)get_or_create_widget(FragmentType.TEXT));
            label.css_classes = { };
            label.visible = true;
            label.set_text(text);
            label.set_attributes(attrs);
        }

        internal void set_description(Description desc) {
            reset();
            process_description(desc, this);
            hide_unused_widgets();
        }

        private void process_description(Description desc, Gtk.Widget parent_widget) {
            switch (desc.fragment_type) {
                case FragmentType.IMAGE:
                    if (parent_widget == this) {
                        unowned TextImage icon = (TextImage)get_or_create_widget(FragmentType.IMAGE);
                        icon.update_icon_name(desc.text);
                        icon.set_data("fragment", null);
                        icon.css_classes = { desc.css_class };
                        if (desc.fragment_func != null) {
                            icon.set_data("fragment", desc);
                            icon.add_css_class("clickable");
                        }
                        icon.visible = true;
                    } else {
                        // For nested items, create new widgets directly
                        var icon = new TextImage();
                        icon.update_icon_name(desc.text);
                        icon.set_data("fragment", null);
                        icon.css_classes = { desc.css_class };
                        if (desc.fragment_func != null) {
                            icon.set_data("fragment", desc);
                            icon.add_css_class("clickable");
                        }
                        icon.visible = true;
                        ((Gtk.Box)parent_widget).append(icon);
                    }
                    break;

                case FragmentType.TEXT:
                    if (parent_widget == this) {
                        unowned Gtk.Label label = (Gtk.Label)get_or_create_widget(FragmentType.TEXT);
                        label.css_classes = { desc.css_class };
                        if (desc.text != label.label) {
                            label.set_text(desc.text);
                        }
                        label.set_attributes(desc.attributes);
                        if (desc.fragment_func != null) {
                            label.set_data("fragment", desc);
                            label.add_css_class("clickable");
                        }
                        label.visible = true;
                    } else {
                        // For nested items, create new widgets directly
                        var label = new Gtk.Label("") {
                            xalign = 0.0f,
                            single_line_mode = true,
                            hexpand = true,
                            valign = Gtk.Align.BASELINE_CENTER,
                            vexpand = true,
                            overflow = Gtk.Overflow.VISIBLE,
                        };
                        label.css_classes = { desc.css_class };
                        if (desc.text != label.label) {
                            label.set_text(desc.text);
                        }
                        label.set_attributes(desc.attributes);

                        if (desc.fragment_func != null) {
                            label.set_data("fragment", desc);
                            label.add_css_class("clickable");
                        }
                        label.visible = true;
                        ((Gtk.Box)parent_widget).append(label);
                    }
                    break;

                case FragmentType.CONTAINER:
                    if (parent_widget == this) {
                        unowned Gtk.Box container = (Gtk.Box)get_or_create_widget(FragmentType.CONTAINER, desc.orientation, desc.spacing);
                        container.css_classes = { desc.css_class };
                        container.visible = true;

                        // Clear existing children in reused containers
                        unowned Gtk.Widget? child = container.get_first_child();
                        while (child != null) {
                            unowned Gtk.Widget? next = child.get_next_sibling();
                            container.remove(child);
                            child = next;
                        }

                        // Process children recursively
                        if (desc.children != null) {
                            foreach (var child_desc in desc.children) {
                                process_description(child_desc, container);
                            }
                        }
                    } else {
                        // For nested containers, create new widgets directly
                        var container = new Gtk.Box(desc.orientation, desc.spacing) {
                            hexpand = true,
                            vexpand = true,
                        };
                        container.css_classes = { desc.css_class };
                        container.visible = true;
                        ((Gtk.Box)parent_widget).append(container);

                        // Process children recursively
                        if (desc.children != null) {
                            foreach (var child_desc in desc.children) {
                                process_description(child_desc, container);
                            }
                        }
                    }
                    break;
            }
        }

        private void hide_all_widgets() {
            for (int i = 0; i < labels.length; i++) {
                labels.get(i).visible = false;
            }
            for (int i = 0; i < images.length; i++) {
                images.get(i).visible = false;
            }
            for (int i = 0; i < containers.length; i++) {
                containers.get(i).visible = false;
            }
        }

        private void hide_unused_widgets() {
            for (int i = visible_labels; i < labels.length; i++) {
                labels.get(i).visible = false;
            }
            for (int i = visible_images; i < images.length; i++) {
                images.get(i).visible = false;
            }
            for (int i = visible_containers; i < containers.length; i++) {
                containers.get(i).visible = false;
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
                sibling.allocate(widget_width, height, max_baseline, transform);
                transform = transform.translate({widget_width, 0});
                sibling = sibling.get_next_sibling();
            }
        }

        private int max_baseline = 0;

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
                    int child_height, child_nat_baseline;
                    sibling.measure(Gtk.Orientation.VERTICAL, -1, null, out child_height, null, out child_nat_baseline);
                    natural = int.max(child_height, natural);
                    max_baseline = minimum_baseline = natural_baseline = int.max(child_nat_baseline, natural_baseline);
                    sibling = sibling.get_next_sibling();
                    minimum = max_baseline;
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
        }

        internal override Gtk.SizeRequestMode get_request_mode() {
            return Gtk.SizeRequestMode.CONSTANT_SIZE;
        }
    }
}
