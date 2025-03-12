namespace StringUtils {
    [CCode (cname = "is_word_boundary", cheader_filename = "string-utils.h")]
    public static bool is_word_boundary(unichar c);

    [CCode (cname = "replace", cheader_filename = "string-utils.h")]
    public static string? replace(string? str, string? chars, string? replacement);

    [CCode (cname = "decode_html_chars")]
    public string? decode_html_chars(string? input);
}
