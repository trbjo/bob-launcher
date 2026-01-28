#include <stdatomic.h>
#include <immintrin.h>
#include <assert.h>
#include <glib.h>
#include <gtk/gtk.h>
#include "file-monitor.h"

#define FALLBACK "image-missing"
#define BOB_LAUNCHER_OBJECT_PATH "/io/github/trbjo/bob/launcher"
#define DEBOUNCE_TIMEOUT_MS 100

static atomic_int lock_token;
static GHashTable *mime_type_map;
static GHashTable *icon_cache;
static GtkIconTheme *theme;
static file_monitor *monitor;
static guint debounce_timer_id = 0;

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

static gboolean debounced_cache_clear(gpointer user_data) {
    clear_cache();
    debounce_timer_id = 0;
    return G_SOURCE_REMOVE;
}

static void on_file_change(const char* path, int event_type, void* user_data) {
    if (debounce_timer_id != 0) {
        g_source_remove(debounce_timer_id);
    }

    debounce_timer_id = g_timeout_add(DEBOUNCE_TIMEOUT_MS, debounced_cache_clear, NULL);
}

static void setup_directory_monitoring() {
    if (monitor != NULL) {
        file_monitor_destroy(monitor);
        monitor = NULL;
    }

    monitor = file_monitor_new(on_file_change, NULL, NULL);
    if (monitor == NULL) {
        return;
    }

    char **search_path = NULL;
    int n_elements = 0;

    g_object_get(theme, "search-path", &search_path, NULL);

    if (search_path == NULL) return;
    for (n_elements = 0; search_path[n_elements] != NULL; n_elements++) { }

    if (n_elements > 0) {
        file_monitor_add_paths_recursive(monitor, (const char**)search_path, n_elements);
    }

    g_strfreev(search_path);
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

static void on_theme_changed(GtkIconTheme *theme, gpointer user_data) {
    clear_cache();
    setup_directory_monitoring();
}

void icon_cache_service_initialize() {
    icon_cache = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_object_unref);
    mime_type_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
    gtk_icon_theme_add_resource_path(theme, BOB_LAUNCHER_OBJECT_PATH);

    g_signal_connect(theme, "changed", G_CALLBACK(on_theme_changed), NULL);

    setup_directory_monitoring();
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
