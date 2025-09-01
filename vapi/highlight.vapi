[CCode (cheader_filename = "highlight.h")]
namespace Highlight {
    [CCode (cname = "HighlightStyle", cprefix = "HIGHLIGHT_STYLE_", has_type_id = false)]
    [Flags]
    internal enum Style {
        COLOR = 1 << 0,
        UNDERLINE = 1 << 1,
        BOLD = 1 << 2,
        BACKGROUND = 1 << 3,
        STRIKETHROUGH = 1 << 4
    }

    [CCode (cname = "HighlightPositions", free_function = "highlight_positions_free")]
    [Compact]
    internal class Positions {
        [CCode (cname = "highlight_calculate_positions")]
        public Positions(Levensteihn.StringInfo needle, string text);
    }

    [CCode (cname = "highlight_set_accent_color")]
    internal static void set_accent_color(string? color);

    [CCode (cname = "highlight_get_accent_color")]
    internal static unowned Gdk.RGBA? get_accent_color();

    [CCode (cname = "highlight_get_pango_accent")]
    internal static unowned string? get_pango_accent();

    [CCode (cname = "highlight_apply_style")]
    internal static Pango.AttrList apply_style(Positions positions, Style style, Gdk.RGBA color);

    [CCode (cname = "highlight_apply_style_range")]
    internal static Pango.AttrList apply_style_range(Positions positions, Style style, Gdk.RGBA color, uint start, uint end);

    [CCode (cname = "highlight_create_attr_list")]
    internal static Pango.AttrList create_attr_list(Levensteihn.StringInfo needle, string text, Style style);
}
