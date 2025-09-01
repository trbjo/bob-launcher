namespace BobLauncher {
    namespace DragAndDropHandler {
        internal delegate unowned Match? MatchFinderFunc(double x, double y);

        internal static void setup(Gtk.Widget widget, MatchFinderFunc match_finder) {
            var drag_source = new Gtk.DragSource();
            widget.add_controller(drag_source);

            drag_source.prepare.connect((source_origin, x, y) => {
                unowned Match? m = match_finder(x, y);
                if (!(m is FileMatch)) {
                    drag_source.set_state(Gtk.EventSequenceState.DENIED);
                    return null;
                }
                drag_source.set_state(Gtk.EventSequenceState.CLAIMED);

                unowned FileMatch um = (FileMatch)m;
                unowned Gtk.IconTheme icon_theme = Gtk.IconTheme.get_for_display(Gdk.Display.get_default());
                var paintable = icon_theme.lookup_icon(
                    um.get_icon_name(),
                    null,
                    48,
                    widget.get_scale_factor(),
                    Gtk.TextDirection.NONE,
                    Gtk.IconLookupFlags.PRELOAD
                );
                drag_source.set_icon(paintable, 0, 0);

                var uri_provider = new Gdk.ContentProvider.for_bytes("text/uri-list", new Bytes(um.get_uri().data));

                var gnome_copied_files_provider = new Gdk.ContentProvider.for_bytes(
                    "application/x-gnome-copied-files",
                    new Bytes(("copy\n" + um.get_uri()).data)
                );

                var text_plain_provider = new Gdk.ContentProvider.for_bytes(
                    "text/plain",
                    new Bytes(File.new_for_uri(um.get_uri()).get_path().data)
                );

                if (um.get_mime_type().has_prefix("text")) {
                    var file_provider = new Gdk.ContentProvider.for_bytes(um.get_mime_type(), Utils.load_file_content(um.get_uri()));
                    return new Gdk.ContentProvider.union({
                        uri_provider,
                        file_provider,
                        gnome_copied_files_provider,
                        text_plain_provider
                    });
                } else {
                    return new Gdk.ContentProvider.union({
                        uri_provider,
                        gnome_copied_files_provider,
                        text_plain_provider
                    });
                }
            });

            bool drop_accepted = false;
            bool ctrl_pressed = false;
            drag_source.drag_begin.connect((drag) => {
                var display = drag.get_device().get_display();
                var seat = display.get_default_seat();
                var keyboard = seat.get_keyboard();

                if (keyboard != null) {
                    var modifier_state = keyboard.get_modifier_state();
                    ctrl_pressed = (modifier_state & Gdk.ModifierType.CONTROL_MASK) != 0;
                }

                drop_accepted = false;
                drag.drop_performed.connect(() => {
                    drop_accepted = true;
                    drag_source.reset();
                });

                drag.dnd_finished.connect(() => {
                    if (!ctrl_pressed && drop_accepted) {
                        Controller.on_drag_and_drop_done();
                    }
                    drag_source.reset();
                    ctrl_pressed = false;
                    drop_accepted = false;
                });
            });

            drag_source.drag_cancel.connect((source_origin, drag, reason) => false);
        }
    }
}
