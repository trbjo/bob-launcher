#include "icon-cache-service.h"
#include "icon-resolver.h"
#include "icon-renderer.h"
#include "file-monitor.h"

#include <stdatomic.h>
#include <immintrin.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/timerfd.h>
#include <glib.h>
#include <gio/gio.h>

#define FALLBACK "image-missing"
#define BOB_LAUNCHER_OBJECT_PATH "/io/github/trbjo/bob/launcher"
#define DEBOUNCE_TIMEOUT_MS 100

/* ================================================================
 * State
 * ================================================================ */

static atomic_int lock_token;
static GHashTable *mime_type_map;
static GHashTable *icon_cache;
static IconResolver *resolver;
static GSettings *interface_settings;
static file_monitor *monitor;
static char **search_paths;
static int n_search_paths;

/* Debounce thread: owns its own timerfd.  on_file_change re-arms the
 * timerfd from whichever thread file_monitor dispatches on (kernel-atomic,
 * no locks needed); the dedicated worker thread blocks on read() and
 * runs the rebuild when the timer expires.  Self-contained — main is
 * never woken for icon-theme changes. */
static int             debounce_fd      = -1;
static pthread_t       debounce_thread;
static atomic_bool     debounce_running = false;

static void on_file_change(const char *path, int event_type, void *user_data);

static inline void spin_lock(atomic_int *lock) {
    while (atomic_exchange(lock, 1)) {
        _mm_pause();
    }
}

static inline void spin_unlock(atomic_int *lock) {
    atomic_store(lock, 0);
}

static unsigned char* read_file_bytes(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);

    unsigned char *buf = malloc(len);
    if (fread(buf, 1, len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return NULL;
    }

    fclose(f);
    *out_len = len;
    return buf;
}

static unsigned char* read_resource_bytes(const char *path, size_t *out_len) {
    GBytes *gbytes = g_resources_lookup_data(path, 0, NULL);
    if (!gbytes) return NULL;

    size_t len;
    const void *data = g_bytes_get_data(gbytes, &len);
    unsigned char *buf = malloc(len);
    memcpy(buf, data, len);
    g_bytes_unref(gbytes);

    *out_len = len;
    return buf;
}

static unsigned char* read_icon_bytes(IconLocation *loc, size_t *out_len) {
    if (loc->is_resource)
        return read_resource_bytes(loc->path, out_len);
    else
        return read_file_bytes(loc->path, out_len);
}

/* ================================================================
 * Rendering from location
 * ================================================================ */

static GdkTexture* render_icon(IconLocation *loc, int pixel_size) {
    size_t len;
    unsigned char *data = read_icon_bytes(loc, &len);
    if (!data) return NULL;

    GdkTexture *tex;
    if (loc->format == ICON_FORMAT_SVG)
        tex = icon_render_svg(data, len, pixel_size);
    else
        tex = icon_render_png(data, len, pixel_size);

    free(data);
    return tex;
}

/* ================================================================
 * Paintable: texture rendered at size * scale, reported as size
 * ================================================================ */

typedef struct {
    GObject parent_instance;
    GdkTexture *texture;
    int size;
} BobIconPaintable;

typedef struct {
    GObjectClass parent_class;
} BobIconPaintableClass;

static gpointer bob_icon_paintable_parent_class = NULL;

static void bob_icon_paintable_snapshot(GdkPaintable *paintable, GdkSnapshot *snapshot,
                                        double width, double height) {
    BobIconPaintable *self = (BobIconPaintable *)paintable;
    gdk_paintable_snapshot(GDK_PAINTABLE(self->texture), snapshot, width, height);
}

static GdkPaintableFlags bob_icon_paintable_get_flags(GdkPaintable *paintable) {
    (void)paintable;
    return GDK_PAINTABLE_STATIC_SIZE | GDK_PAINTABLE_STATIC_CONTENTS;
}

static int bob_icon_paintable_get_intrinsic_size(GdkPaintable *paintable) {
    return ((BobIconPaintable *)paintable)->size;
}

static void bob_icon_paintable_iface_init(GdkPaintableInterface *iface) {
    iface->snapshot = bob_icon_paintable_snapshot;
    iface->get_flags = bob_icon_paintable_get_flags;
    iface->get_intrinsic_width = bob_icon_paintable_get_intrinsic_size;
    iface->get_intrinsic_height = bob_icon_paintable_get_intrinsic_size;
}

static void bob_icon_paintable_finalize(GObject *object) {
    g_clear_object(&((BobIconPaintable *)object)->texture);
    G_OBJECT_CLASS(bob_icon_paintable_parent_class)->finalize(object);
}

static void bob_icon_paintable_class_init(BobIconPaintableClass *klass, gpointer klass_data) {
    (void)klass_data;
    bob_icon_paintable_parent_class = g_type_class_peek_parent(klass);
    G_OBJECT_CLASS(klass)->finalize = bob_icon_paintable_finalize;
}

static GType bob_icon_paintable_get_type(void) {
    static gsize type_id = 0;
    if (g_once_init_enter(&type_id)) {
        static const GTypeInfo info = {
            sizeof(BobIconPaintableClass), NULL, NULL,
            (GClassInitFunc)bob_icon_paintable_class_init, NULL, NULL,
            sizeof(BobIconPaintable), 0, NULL, NULL
        };
        static const GInterfaceInfo paintable_info = {
            (GInterfaceInitFunc)bob_icon_paintable_iface_init, NULL, NULL
        };
        GType id = g_type_register_static(G_TYPE_OBJECT, "BobIconPaintable", &info, 0);
        g_type_add_interface_static(id, GDK_TYPE_PAINTABLE, &paintable_info);
        g_once_init_leave(&type_id, id);
    }
    return type_id;
}

/* Takes ownership of texture. */
static GdkPaintable *bob_icon_paintable_new(GdkTexture *texture, int size) {
    BobIconPaintable *self = g_object_new(bob_icon_paintable_get_type(), NULL);
    self->texture = texture;
    self->size = size;
    return GDK_PAINTABLE(self);
}

/* ================================================================
 * Search paths
 * ================================================================ */

static void build_search_paths(void) {
    GPtrArray *paths = g_ptr_array_new();

    g_ptr_array_add(paths, g_build_filename(g_get_user_data_dir(), "icons", NULL));
    g_ptr_array_add(paths, g_build_filename(g_get_home_dir(), ".icons", NULL));

    const char *const *data_dirs = g_get_system_data_dirs();
    for (int i = 0; data_dirs[i]; i++)
        g_ptr_array_add(paths, g_build_filename(data_dirs[i], "icons", NULL));

    n_search_paths = paths->len;
    search_paths = (char **)g_ptr_array_free(paths, FALSE);
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

static void rebuild_resolver(void) {
    char *theme_name = g_settings_get_string(interface_settings, "icon-theme");
    const char *res_paths[] = { BOB_LAUNCHER_OBJECT_PATH };

    IconResolver *new_resolver = icon_resolver_new(
        theme_name,
        (const char **)search_paths, n_search_paths,
        res_paths, 1);

    spin_lock(&lock_token);
    IconResolver *old = resolver;
    resolver = new_resolver;
    g_hash_table_remove_all(icon_cache);
    g_hash_table_remove_all(mime_type_map);
    spin_unlock(&lock_token);

    icon_resolver_destroy(old);
    g_free(theme_name);
}

/* ================================================================
 * File monitoring
 * ================================================================ */

static void setup_directory_monitoring(void) {
    if (monitor) {
        file_monitor_destroy(monitor);
        monitor = NULL;
    }

    monitor = file_monitor_new(on_file_change, NULL, NULL);
    if (!monitor) return;

    if (n_search_paths > 0)
        file_monitor_add_paths_recursive(monitor, (const char **)search_paths, n_search_paths);
}

/* Debounce worker.  Sleeps in read() on the timerfd; wakes only when
 * on_file_change has armed it, or at shutdown when icon_cache_service_shutdown
 * fires it explicitly to break the read.  The rebuild work itself runs
 * here so a slow icon_resolver_new (large theme directories) doesn't
 * block any other thread. */
static void *debounce_thread_func(void *unused) {
    (void)unused;
    for (;;) {
        uint64_t expirations;
        ssize_t n = read(debounce_fd, &expirations, sizeof expirations);
        if (n != (ssize_t)sizeof expirations) {
            if (n < 0 && errno == EINTR) continue;
            /* fd closed (EBADF) or other unrecoverable error → exit. */
            break;
        }
        if (!atomic_load_explicit(&debounce_running, memory_order_acquire))
            break;
        rebuild_resolver();
        setup_directory_monitoring();
    }
    return NULL;
}

static void on_file_change(const char *path, int event_type, void *user_data) {
    (void)path;
    (void)event_type;
    (void)user_data;
    /* Re-arm: itimerspec replaces any pending expiration atomically, which
     * gives us trailing-edge debounce semantics for free.  Bursts of inotify
     * events (an icon pack install drops dozens) collapse to a single fire
     * DEBOUNCE_TIMEOUT_MS after the last one.  Safe to call from any thread
     * — timerfd_settime is a kernel syscall, no userspace state mutated. */
    struct itimerspec its = {
        .it_value = { .tv_sec = 0, .tv_nsec = DEBOUNCE_TIMEOUT_MS * 1000000L },
    };
    timerfd_settime(debounce_fd, 0, &its, NULL);
}

static void on_icon_theme_changed(GSettings *settings, const char *key, gpointer user_data) {
    rebuild_resolver();
}

/* ================================================================
 * Public API
 * ================================================================ */

void icon_cache_service_initialize(void) {
    icon_cache = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_object_unref);
    mime_type_map = g_hash_table_new_full(g_str_hash, g_str_equal, free, free);

    build_search_paths();

    interface_settings = g_settings_new("org.gnome.desktop.interface");
    g_signal_connect(interface_settings, "changed::icon-theme",
                     G_CALLBACK(on_icon_theme_changed), NULL);

    rebuild_resolver();

    /* Start the debounce thread before setup_directory_monitoring so
     * on_file_change has a valid fd if the very first inotify event
     * arrives before this function returns. */
    debounce_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    atomic_store_explicit(&debounce_running, true, memory_order_release);
    pthread_create(&debounce_thread, NULL, debounce_thread_func, NULL);

    setup_directory_monitoring();
}

void icon_cache_service_shutdown(void) {
    /* Tear down monitoring first: no more on_file_change calls means no
     * more timerfd_settime races against the fd we're about to close. */
    if (monitor) {
        file_monitor_destroy(monitor);
        monitor = NULL;
    }

    if (debounce_fd < 0) return;

    /* Flip the run flag, then arm the timerfd for an immediate fire to
     * unblock the read() the worker is sitting in.  The worker re-checks
     * the flag right after read() returns and exits. */
    atomic_store_explicit(&debounce_running, false, memory_order_release);
    struct itimerspec its = { .it_value = { .tv_sec = 0, .tv_nsec = 1 } };
    timerfd_settime(debounce_fd, 0, &its, NULL);

    pthread_join(debounce_thread, NULL);
    close(debounce_fd);
    debounce_fd = -1;
}

GdkPaintable* icon_cache_service_get_paintable_for_icon_name(const char *icon_name, int size, int scale) {
    uint key = compute_composite_hash(icon_name, size, scale);

    /* Check cache */
    spin_lock(&lock_token);
    GdkPaintable *p = g_hash_table_lookup(icon_cache, GUINT_TO_POINTER(key));
    if (p) {
        spin_unlock(&lock_token);
        return p;
    }

    /* Resolve under lock (fast, in-memory) */
    IconLocation loc = {0};
    bool found = icon_resolver_lookup(resolver, icon_name, size, scale, &loc);
    if (!found)
        found = icon_resolver_lookup(resolver, FALLBACK, size, scale, &loc);
    spin_unlock(&lock_token);

    if (!found) return NULL;

    /* Read and render outside lock (slow, concurrent) */
    GdkTexture *tex = render_icon(&loc, size * scale);
    icon_location_free(&loc);

    if (!tex) return NULL;

    p = bob_icon_paintable_new(tex, size);

    /* Insert with race check */
    spin_lock(&lock_token);
    GdkPaintable *existing = g_hash_table_lookup(icon_cache, GUINT_TO_POINTER(key));
    if (existing) {
        spin_unlock(&lock_token);
        g_object_unref(p);
        return existing;
    }
    g_hash_table_insert(icon_cache, GUINT_TO_POINTER(key), p);
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
    IconLocation loc;

    for (int i = 0; names[i]; i++) {
        if (icon_resolver_lookup(resolver, names[i], 48, 1, &loc)) {
            result = names[i];
            icon_location_free(&loc);
            break;
        }
    }

    char *result_copy = strdup(result);
    g_hash_table_insert(mime_type_map, strdup(content_type), result_copy);
    spin_unlock(&lock_token);

    g_object_unref(icon);

    return result_copy;
}
