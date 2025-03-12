namespace StyleProvider {
    // TODO: Remove when merged
    // https://gitlab.gnome.org/GNOME/vala/-/merge_requests/312/
    [CCode (cname = "gtk_style_context_add_provider_for_display")]
    extern static void add_provider_for_display(Gdk.Display display, Gtk.StyleProvider provider, uint priority);

    // https://gitlab.gnome.org/GNOME/vala/-/merge_requests/312/
    [CCode (cname = "gtk_style_context_remove_provider_for_display")]
    extern static void remove_provider_for_display(Gdk.Display display, Gtk.StyleProvider provider);
}
