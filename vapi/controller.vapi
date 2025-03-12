[CCode (cheader_filename = "controller.h", cname = "controller_", lower_case_cprefix = "controller_")]
namespace Controller {
    internal static void init();
    internal static unowned BobLauncher.Match? selected_match();
    internal static void goto_match(int relative_index);
    internal static void goto_match_abs(int abs_index);
    internal static void on_drag_and_drop_done();
    internal static void execute(bool should_hide);
    internal static void start_search(string search_query);
    internal static bool select_plugin(string plugin, string? query = null);
    internal static void handle_key_release(uint keyval, Gdk.ModifierType state);
    internal static void handle_key_press(uint keyval, Gdk.ModifierType state);
    internal static void handle_focus_enter();
    internal static void handle_focus_leave();
}
