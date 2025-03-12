namespace InputRegion {
    private static int pack_ints(int a, int b) {
        return ((int)a << 16) | (int)b;
    }

    private static GLib.HashTable<int, Cairo.Region> region_table;

    internal static void initialize() {
        region_table = new GLib.HashTable<int, Cairo.Region>(direct_hash, direct_equal);
    }

    internal static void reset() {
        region_table.remove_all();
    }

    internal static Cairo.Region create_image_surface(Gtk.Window win, Gdk.Surface surface) {
        Graphene.Rect inner;
        if (!win.compute_bounds(win.child, out inner)) critical("could not calculate bounds");
        var snapshot = new Gtk.Snapshot();
        win.snapshot(snapshot);
        var node = snapshot.to_node();
        if (node == null) critical("node is null");
        var cairo_surf = new Cairo.ImageSurface(Cairo.Format.A8, (int)(inner.size.width + inner.origin.x), (int)(inner.size.height + inner.origin.y));
        node.draw(new Cairo.Context(cairo_surf));
        return Gdk.cairo_region_create_from_surface(cairo_surf);
    }

    internal static void set_input_regions(Gtk.Window win, int width, int height) {
        Gdk.Surface? surface = win.get_surface();
        if (surface == null) critical("could not create surface");
        int lookupkey = pack_ints(width, height);
        Cairo.Region? reg = region_table.get(lookupkey);
        if (reg == null) {
            reg = create_image_surface(win, surface);
            region_table.set(lookupkey, reg);
        }

        surface.set_input_region(reg);
    }
}
