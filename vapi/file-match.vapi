namespace BobLauncher {
    [CCode (cheader_filename = "file-match.h", cname = "BobLauncherFileMatch", type_id = "bob_launcher_file_match_get_type()")]
    public class FileMatch : BobLauncher.Match, BobLauncher.IFile, BobLauncher.IRichDescription {
        [CCode (cname = "BOB_LAUNCHER_FILE_MATCH_SEARCH_FILE_ATTRIBUTES")]
        public const string SEARCH_FILE_ATTRIBUTES;

        [CCode (cname = "bob_launcher_file_match_new_from_path", has_construct_function = false)]
        public FileMatch.from_path(string filename);

        [CCode (cname = "bob_launcher_file_match_new_from_uri", has_construct_function = false)]
        public FileMatch.from_uri(string uri);

        [CCode (cname = "bob_launcher_file_match_get_filename")]
        public unowned string get_filename();

        [CCode (cname = "bob_launcher_file_match_get_timestamp")]
        public unowned GLib.DateTime? get_timestamp();

        [CCode (cname = "bob_launcher_file_match_set_timestamp")]
        public void set_timestamp(GLib.DateTime? value);

        [CCode (cname = "bob_launcher_file_match_get_file_info")]
        public unowned GLib.FileInfo get_file_info();

        [CCode (cname = "bob_launcher_file_match_rehighlight_matches")]
        public void rehighlight_matches();

        [CCode (cname = "bob_launcher_file_match_generate_description_for_file")]
        public static Description generate_description_for_file(Levensteihn.StringInfo si, string file_path, GLib.DateTime? timestamp);

        [CCode (cname = "bob_launcher_file_match_split_path_with_separators")]
        public static GLib.GenericArray<string> split_path_with_separators(string path);

        public string filename { get; construct; }
        public GLib.DateTime? timestamp { get; set; }
    }
}
