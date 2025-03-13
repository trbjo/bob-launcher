#include <stdatomic.h>
#include <immintrin.h>
#include <assert.h>
#include <glib.h>
#include <gtk/gtk.h>

#define FALLBACK "image-missing"
#define BOB_LAUNCHER_OBJECT_PATH "/io/github/trbjo/bob/launcher"

static atomic_int lock_token;
static GHashTable *mime_type_map;
static GHashTable *icon_cache;
static GtkIconTheme *theme;

static inline void spin_lock(atomic_int *lock) {
    while (atomic_exchange(lock, 1)) {
        _mm_pause();
    }
}

static inline void spin_unlock(atomic_int *lock) {
    atomic_store(lock, 0);
}

static void clear_cache() {
    spin_lock(&lock_token);
    g_hash_table_remove_all(mime_type_map);
    g_hash_table_remove_all(icon_cache);
    spin_unlock(&lock_token);
}

static uint compute_composite_hash(const char *str, int size, int scale_factor) {
    uint str_hash = g_str_hash(str);

    // Ensure size is positive and within reasonable bounds
    assert(size > 0 && size <= 512);
    // Ensure scale factor is within reasonable bounds (1-4)
    assert(scale_factor > 0 && scale_factor <= 4);

    // Reserve:
    // - Lower 9 bits for size (allows sizes up to 512)
    // - Next 2 bits for scale factor (allows values 1-4)
    // - Upper 21 bits for string hash
    uint size_part = (uint)(size & 0x1FF);                   // Take lower 9 bits of size
    uint scale_part = (uint)((scale_factor - 1) & 0x3) << 9; // Take 2 bits, shifted left by 9
    uint str_part = (str_hash >> 11) << 11;                  // Clear lower 11 bits, shifted back

    return str_part | scale_part | size_part;
}

void icon_cache_service_initialize() {
    icon_cache = g_hash_table_new(g_direct_hash, g_direct_equal);
    mime_type_map = g_hash_table_new(g_str_hash, g_str_equal);

    theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
    gtk_icon_theme_add_resource_path(theme, BOB_LAUNCHER_OBJECT_PATH);
    g_signal_connect(theme, "changed", G_CALLBACK(clear_cache), NULL);
}

GdkPaintable* icon_cache_service_get_paintable_for_icon_name(const char *icon_name, int size, int scale) {
    spin_lock(&lock_token);

    uint key = compute_composite_hash(icon_name, size, scale);
    GdkPaintable *p = g_hash_table_lookup(icon_cache, GUINT_TO_POINTER(key));

    if (p == NULL) {
        const char *lookup_str;
        bool has_icon = gtk_icon_theme_has_icon(theme, icon_name);
        lookup_str = has_icon ? icon_name : FALLBACK;

        GtkIconPaintable *paintable = gtk_icon_theme_lookup_icon(
            theme,
            lookup_str,
            NULL,
            size,
            scale,
            GTK_TEXT_DIR_NONE,
            0
        );

        p = GDK_PAINTABLE(paintable);
        g_hash_table_insert(icon_cache, GUINT_TO_POINTER(key), p);
    }

    spin_unlock(&lock_token);
    return p;
}

const char* icon_cache_service_best_icon_name_for_mime_type(const char *content_type) {
    if (content_type == NULL) return FALLBACK;

    spin_lock(&lock_token);

    const char *mime_icon = g_hash_table_lookup(mime_type_map, content_type);
    if (mime_icon != NULL) {
        spin_unlock(&lock_token);
        return mime_icon;
    }

    GThemedIcon *icon = G_THEMED_ICON(g_content_type_get_icon(content_type));

    if (icon == NULL) {
        g_warning("match not found: %s", content_type);
        spin_unlock(&lock_token);
        return FALLBACK;
    }

    const char* const* names = g_themed_icon_get_names(icon);
    const char *result = FALLBACK;

    for (int i = 0; names[i] != NULL; i++) {
        if (gtk_icon_theme_has_icon(theme, names[i])) {
            result = names[i];
            break;
        }
    }

    char *result_copy = g_strdup(result);
    g_hash_table_insert(mime_type_map, g_strdup(content_type), result_copy);

    g_object_unref(icon);
    spin_unlock(&lock_token);
    return result_copy;
}
