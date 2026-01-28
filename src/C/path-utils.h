#ifndef PATH_UTILS_H
#define PATH_UTILS_H

#include <string.h>
#include <stdlib.h>

/*
 * Try to find an existing file by stripping :N suffix components.
 * Strips at most max_strips components (typically 2 for :line:column).
 *
 * Returns: pointer within path where suffix starts, or NULL if no valid split found.
 */
static inline const char *
path_find_existing_base(const char *path, int (*exists_fn)(const char *), int max_strips)
{
    if (exists_fn(path)) {
        return NULL;
    }

    size_t len = strlen(path);
    const char *end = path + len;

    for (int i = 0; i < max_strips; i++) {
        const char *colon = end - 1;
        while (colon > path && *colon != ':') {
            colon--;
        }

        if (colon <= path || *colon != ':') {
            break;
        }

        if (colon[1] < '0' || colon[1] > '9') {
            break;
        }

        size_t base_len = colon - path;
        char *base = malloc(base_len + 1);
        if (!base) break;
        memcpy(base, path, base_len);
        base[base_len] = '\0';

        int found = exists_fn(base);
        free(base);

        if (found) {
            return colon;
        }

        end = colon;
    }

    return NULL;
}

/*
 * Check if suffix has column (two colons like :10:5) or just line (:10).
 * Returns 1 if suffix has column, 0 if only line.
 */
static inline int
suffix_has_column(const char *suffix)
{
    if (!suffix || *suffix != ':') return 0;
    const char *p = suffix + 1;
    while (*p >= '0' && *p <= '9') p++;
    return (*p == ':');
}

#endif
