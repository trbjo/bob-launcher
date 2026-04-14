#include "icon-resolver.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <dirent.h>
#include <gio/gio.h>

/* ================================================================
 * Internal types
 * ================================================================ */

typedef struct {
    char *path;
    IconFormat format;
    bool is_resource;
    int size;
    int scale;
    int theme_priority;
} IconEntry;

typedef struct {
    IconEntry *items;
    int len;
    int cap;
} IconEntryList;

typedef struct {
    char *subdir;
    int size;
    int scale;
} ThemeDir;

struct IconResolver {
    GHashTable *icons;
};

/* ================================================================
 * Entry list
 * ================================================================ */

static void entry_list_add(IconEntryList *list, IconEntry *entry) {
    if (list->len >= list->cap) {
        list->cap = list->cap ? list->cap * 2 : 4;
        list->items = realloc(list->items, list->cap * sizeof(IconEntry));
    }
    list->items[list->len++] = *entry;
}

static void entry_list_free(gpointer data) {
    IconEntryList *list = data;
    for (int i = 0; i < list->len; i++)
        free(list->items[i].path);
    free(list->items);
    free(list);
}

/* ================================================================
 * Helpers
 * ================================================================ */

static bool strip_extension(const char *filename, char *out, size_t out_size, IconFormat *fmt) {
    size_t len = strlen(filename);
    if (len > 4 && strcmp(filename + len - 4, ".svg") == 0) {
        *fmt = ICON_FORMAT_SVG;
    } else if (len > 4 && strcmp(filename + len - 4, ".png") == 0) {
        *fmt = ICON_FORMAT_PNG;
    } else {
        return false;
    }
    size_t name_len = len - 4;
    if (name_len >= out_size) return false;
    memcpy(out, filename, name_len);
    out[name_len] = '\0';
    return true;
}

static void add_entry(GHashTable *icons, const char *icon_name, IconEntry *entry) {
    IconEntryList *list = g_hash_table_lookup(icons, icon_name);
    if (!list) {
        list = calloc(1, sizeof(IconEntryList));
        g_hash_table_insert(icons, strdup(icon_name), list);
    }
    entry_list_add(list, entry);
}

/* ================================================================
 * index.theme parsing
 * ================================================================ */

static ThemeDir *parse_directories(GKeyFile *kf, int *out_n) {
    char *dir_list = g_key_file_get_string(kf, "Icon Theme", "Directories", NULL);
    if (!dir_list) { *out_n = 0; return NULL; }

    int capacity = 1;
    for (char *p = dir_list; *p; p++)
        if (*p == ',') capacity++;

    ThemeDir *dirs = calloc(capacity, sizeof(ThemeDir));
    int n = 0;

    char *saveptr;
    char *tok = strtok_r(dir_list, ",", &saveptr);
    while (tok && n < capacity) {
        while (*tok == ' ') tok++;
        char *end = tok + strlen(tok) - 1;
        while (end > tok && *end == ' ') *end-- = '\0';
        if (*tok == '\0') { tok = strtok_r(NULL, ",", &saveptr); continue; }

        ThemeDir *d = &dirs[n];
        d->subdir = strdup(tok);
        d->size = g_key_file_get_integer(kf, tok, "Size", NULL);
        d->scale = g_key_file_get_integer(kf, tok, "Scale", NULL);
        if (d->scale < 1) d->scale = 1;

        n++;
        tok = strtok_r(NULL, ",", &saveptr);
    }

    free(dir_list);
    *out_n = n;
    return dirs;
}

static char **parse_inherits(GKeyFile *kf, int *out_n) {
    char *raw = g_key_file_get_string(kf, "Icon Theme", "Inherits", NULL);
    if (!raw) { *out_n = 0; return NULL; }

    int capacity = 1;
    for (char *p = raw; *p; p++)
        if (*p == ',') capacity++;

    char **list = calloc(capacity + 1, sizeof(char *));
    int n = 0;

    char *saveptr;
    char *tok = strtok_r(raw, ",", &saveptr);
    while (tok && n < capacity) {
        while (*tok == ' ') tok++;
        char *end = tok + strlen(tok) - 1;
        while (end > tok && *end == ' ') *end-- = '\0';
        if (*tok) list[n++] = strdup(tok);
        tok = strtok_r(NULL, ",", &saveptr);
    }

    free(raw);
    *out_n = n;
    return list;
}

/* ================================================================
 * Theme chain
 * ================================================================ */

static char *find_theme_base(const char *theme_name, const char **search_paths, int n) {
    char buf[PATH_MAX];
    for (int i = 0; i < n; i++) {
        snprintf(buf, sizeof(buf), "%s/%s/index.theme", search_paths[i], theme_name);
        if (access(buf, R_OK) == 0) {
            snprintf(buf, sizeof(buf), "%s/%s", search_paths[i], theme_name);
            return strdup(buf);
        }
    }
    return NULL;
}

static bool name_in_list(const char *name, char **list, int n) {
    for (int i = 0; i < n; i++)
        if (strcmp(list[i], name) == 0) return true;
    return false;
}

static char **build_theme_chain(const char *root,
                                const char **search_paths, int n_search,
                                int *out_n) {
    int capacity = 16;
    char **chain = calloc(capacity, sizeof(char *));
    int n = 0;

    chain[n++] = strdup(root);
    int head = 0;

    while (head < n) {
        char *name = chain[head++];
        char *base = find_theme_base(name, search_paths, n_search);
        if (!base) continue;

        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/index.theme", base);
        free(base);

        GKeyFile *kf = g_key_file_new();
        if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
            g_key_file_free(kf);
            continue;
        }

        int n_inh;
        char **inh = parse_inherits(kf, &n_inh);
        g_key_file_free(kf);

        for (int i = 0; i < n_inh; i++) {
            if (!name_in_list(inh[i], chain, n)) {
                if (n >= capacity) {
                    capacity *= 2;
                    chain = realloc(chain, capacity * sizeof(char *));
                }
                chain[n++] = inh[i];
            } else {
                free(inh[i]);
            }
        }
        free(inh);
    }

    if (!name_in_list("hicolor", chain, n)) {
        if (n >= capacity) {
            capacity++;
            chain = realloc(chain, capacity * sizeof(char *));
        }
        chain[n++] = strdup("hicolor");
    }

    *out_n = n;
    return chain;
}

/* ================================================================
 * Filesystem scanning
 * ================================================================ */

static void scan_fs_directory(GHashTable *icons, const char *base_path,
                              ThemeDir *dir, int theme_priority) {
    char dirpath[PATH_MAX];
    snprintf(dirpath, sizeof(dirpath), "%s/%s", base_path, dir->subdir);

    DIR *d = opendir(dirpath);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char icon_name[256];
        IconFormat fmt;
        if (!strip_extension(ent->d_name, icon_name, sizeof(icon_name), &fmt))
            continue;

        char fullpath[PATH_MAX];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, ent->d_name);

        IconEntry entry = {
            .path = strdup(fullpath),
            .format = fmt,
            .is_resource = false,
            .size = dir->size,
            .scale = dir->scale,
            .theme_priority = theme_priority,
        };

        add_entry(icons, icon_name, &entry);
    }

    closedir(d);
}

/* ================================================================
 * Resource scanning
 * ================================================================ */

static bool parse_size_dir(const char *name, int *size, int *scale) {
    *scale = 1;

    if (strcmp(name, "scalable") == 0 || strcmp(name, "symbolic") == 0) {
        *size = 512;
        return true;
    }

    int w, h, s;
    if (sscanf(name, "%dx%d@%d", &w, &h, &s) == 3) {
        *size = w;
        *scale = s;
        return true;
    }
    if (sscanf(name, "%dx%d", &w, &h) == 2) {
        *size = w;
        return true;
    }

    return false;
}

static void scan_pixmaps(GHashTable *icons, int theme_priority) {
    const char *dirs[] = { "/usr/share/pixmaps", NULL };

    for (int d = 0; dirs[d]; d++) {
        DIR *dir = opendir(dirs[d]);
        if (!dir) continue;

        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_name[0] == '.') continue;

            char icon_name[256];
            IconFormat fmt;
            if (!strip_extension(ent->d_name, icon_name, sizeof(icon_name), &fmt))
                continue;

            char fullpath[PATH_MAX];
            snprintf(fullpath, sizeof(fullpath), "%s/%s", dirs[d], ent->d_name);

            IconEntry entry = {
                .path = strdup(fullpath),
                .format = fmt,
                .is_resource = false,
                .size = 512,   /* treat as large/scalable */
                .scale = 1,
                .theme_priority = theme_priority,
            };

            add_entry(icons, icon_name, &entry);
        }

        closedir(dir);
    }
}

static void scan_resource_path(GHashTable *icons, const char *res_base, int theme_priority) {
    char base_with_slash[PATH_MAX];
    snprintf(base_with_slash, sizeof(base_with_slash), "%s/", res_base);

    char **size_dirs = g_resources_enumerate_children(base_with_slash, 0, NULL);
    if (!size_dirs) return;

    for (int i = 0; size_dirs[i]; i++) {
        char *sdir = size_dirs[i];
        size_t slen = strlen(sdir);
        if (slen > 0 && sdir[slen - 1] == '/') sdir[slen - 1] = '\0';

        int size, scale;
        if (!parse_size_dir(sdir, &size, &scale))
            continue;

        char size_path[PATH_MAX];
        snprintf(size_path, sizeof(size_path), "%s%s/", base_with_slash, sdir);

        char **ctx_dirs = g_resources_enumerate_children(size_path, 0, NULL);
        if (!ctx_dirs) continue;

        for (int j = 0; ctx_dirs[j]; j++) {
            char *cdir = ctx_dirs[j];
            size_t clen = strlen(cdir);
            if (clen > 0 && cdir[clen - 1] == '/') cdir[clen - 1] = '\0';

            char ctx_path[PATH_MAX];
            snprintf(ctx_path, sizeof(ctx_path), "%s%s/", size_path, cdir);

            char **files = g_resources_enumerate_children(ctx_path, 0, NULL);
            if (!files) continue;

            for (int k = 0; files[k]; k++) {
                char icon_name[256];
                IconFormat fmt;
                if (!strip_extension(files[k], icon_name, sizeof(icon_name), &fmt))
                    continue;

                char fullpath[PATH_MAX];
                snprintf(fullpath, sizeof(fullpath), "%s%s", ctx_path, files[k]);

                IconEntry entry = {
                    .path = strdup(fullpath),
                    .format = fmt,
                    .is_resource = true,
                    .size = size,
                    .scale = scale,
                    .theme_priority = theme_priority,
                };

                add_entry(icons, icon_name, &entry);
            }

            g_strfreev(files);
        }

        g_strfreev(ctx_dirs);
    }

    g_strfreev(size_dirs);
}

/* ================================================================
 * Lookup
 * ================================================================ */

bool icon_resolver_lookup(IconResolver *r, const char *icon_name,
                          int size, int scale, IconLocation *out) {
    IconEntryList *list = g_hash_table_lookup(r->icons, icon_name);
    if (!list || !list->len) return false;

    IconEntry *best = &list->items[list->len - 1];
    IconEntry *last_svg = NULL;

    for (int i = 0; i < list->len; i++) {
        IconEntry *e = &list->items[i];
        if (e->format == ICON_FORMAT_SVG) last_svg = e;
        if (e->size >= size) {
            if (e->format == ICON_FORMAT_PNG && e->size != size && last_svg)
                best = last_svg;
            else
                best = e;
            break;
        }
    }

    out->path = strdup(best->path);
    out->is_resource = best->is_resource;
    out->format = best->format;
    return true;
}

/* ================================================================
 * Construction / destruction
 * ================================================================ */

static int compare_entries(const void *a, const void *b) {
    const IconEntry *ea = a;
    const IconEntry *eb = b;
    if (ea->theme_priority != eb->theme_priority)
        return ea->theme_priority - eb->theme_priority;
    if (ea->format != eb->format)
        return ea->format - eb->format;
    if (ea->size != eb->size)
        return ea->size - eb->size;
    return eb->scale - ea->scale;
}

IconResolver* icon_resolver_new(const char *theme_name,
                                const char **search_paths, int n_search_paths,
                                const char **resource_paths, int n_resource_paths) {
    IconResolver *r = calloc(1, sizeof(IconResolver));
    r->icons = g_hash_table_new_full(g_str_hash, g_str_equal, free, entry_list_free);

    int n_chain;
    char **chain = build_theme_chain(theme_name, search_paths, n_search_paths, &n_chain);

    for (int i = 0; i < n_chain; i++) {
        char *base = find_theme_base(chain[i], search_paths, n_search_paths);
        if (!base) { free(chain[i]); continue; }

        char index_path[PATH_MAX];
        snprintf(index_path, sizeof(index_path), "%s/index.theme", base);
        free(base);

        GKeyFile *kf = g_key_file_new();
        if (!g_key_file_load_from_file(kf, index_path, G_KEY_FILE_NONE, NULL)) {
            g_key_file_free(kf);
            free(chain[i]);
            continue;
        }

        int n_dirs;
        ThemeDir *dirs = parse_directories(kf, &n_dirs);
        g_key_file_free(kf);

        for (int sp = 0; sp < n_search_paths; sp++) {
            char theme_base[PATH_MAX];
            snprintf(theme_base, sizeof(theme_base), "%s/%s", search_paths[sp], chain[i]);
            for (int j = 0; j < n_dirs; j++)
                scan_fs_directory(r->icons, theme_base, &dirs[j], i);
        }

        for (int j = 0; j < n_dirs; j++)
            free(dirs[j].subdir);
        free(dirs);
        free(chain[i]);
    }
    free(chain);

    for (int i = 0; i < n_resource_paths; i++)
        scan_resource_path(r->icons, resource_paths[i], -1);

    scan_pixmaps(r->icons, n_chain);

    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, r->icons);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        IconEntryList *list = value;
        qsort(list->items, list->len, sizeof(IconEntry), compare_entries);
    }

    return r;
}

void icon_resolver_destroy(IconResolver *r) {
    if (!r) return;
    g_hash_table_destroy(r->icons);
    free(r);
}

void icon_location_free(IconLocation *loc) {
    if (loc) free(loc->path);
}
