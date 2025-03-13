[CCode (cheader_filename = "icon-cache-service.h")]
namespace IconCacheService {
    [CCode (cname = "icon_cache_service_initialize")]
    public static void initialize ();

    [CCode (cname = "icon_cache_service_get_paintable_for_icon_name")]
    public static unowned Gdk.Paintable get_paintable_for_icon_name (string icon_name, int size, int scale);

    [CCode (cname = "icon_cache_service_best_icon_name_for_mime_type")]
    public static unowned string best_icon_name_for_mime_type (string? content_type);
}
