namespace Highlight {
    [CCode (cheader_filename = "highlight.h", cname = "highlight_set_accent_color")]
    internal static void set_accent_color(string? color);

    [CCode (cheader_filename = "highlight.h", cname = "highlight_get_accent_color")]
    internal static unowned Gdk.RGBA? get_accent_color();

    [CCode (cheader_filename = "highlight.h", cname = "highlight_get_pango_accent")]
    internal static unowned string? get_pango_accent();

    [CCode (cheader_filename = "highlight.h", cname = "highlight_parse_color")]
    internal static void parse_color(string color, out uint r, out uint g, out uint b, out uint a);

    [CCode (cheader_filename = "highlight.h", cname = "highlight_apply_highlights")]
    internal static string? apply_highlights(string? text, string? highlight_color, [CCode (array_length_type = "size_t")] int[] positions);

    [CCode (cheader_filename = "highlight.h", cname = "highlight_format_highlights")]
    internal static string? format_highlights(string? text, string? highlight_color, Levensteihn.StringInfo si);

    [CCode (cheader_filename = "highlight.h", cname = "match_positions_with_markup")]
    private static int[] match_positions_with_markup(Levensteihn.StringInfo needle, string? haystack);

}
