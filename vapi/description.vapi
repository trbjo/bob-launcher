namespace BobLauncher {
    [CCode (cheader_filename = "description.h", cname = "ClickFunc", has_target = true)]
    public delegate void ClickFunc();

    [CCode (cheader_filename = "description.h", cname = "TextDesc", free_function = "", has_type_id = false)]
    [Compact]
    public class TextDesc {
        public string text;
        public string css_class;
        public Pango.AttrList? attrs;
        public ClickFunc? click_func;

        [CCode (cheader_filename = "description.h", cname = "description_new_text")]
        public TextDesc(string? text, string? css_class, Pango.AttrList? attrs = null, owned ClickFunc? func = null);
    }

    [CCode (cheader_filename = "description.h", cname = "ImageDesc", free_function = "", has_type_id = false)]
    [Compact]
    public class ImageDesc {
        public string icon_name;
        public string css_class;
        public ClickFunc? click_func;

        [CCode (cheader_filename = "description.h", cname = "description_new_image")]
        public ImageDesc(string? icon_name, string? css_class, owned ClickFunc? func = null);
    }

    [CCode (cheader_filename = "description.h", cname = "Description", free_function = "description_free", has_type_id = false)]
    [Compact]
    public class Description {
        public string css_class;
        public ClickFunc? click_func;

        [CCode (cheader_filename = "description.h", cname = "description_new_container")]
        public Description.container(string? css_class = null, owned ClickFunc? func = null);

        [CCode (cheader_filename = "description.h", cname = "description_add_text")]
        public void add_text(owned TextDesc child);

        [CCode (cheader_filename = "description.h", cname = "description_add_image")]
        public void add_image(owned ImageDesc child);

        [CCode (cheader_filename = "description.h", cname = "description_add_container")]
        public void add_container(owned Description child);
    }
}
