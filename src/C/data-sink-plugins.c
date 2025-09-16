#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "result-container.h"
#include "hashset.h"

typedef struct _BobLauncherSearchBase BobLauncherSearchBase;
typedef struct _BobLauncherMatch BobLauncherMatch;
typedef struct _BobLauncherUnknownMatch BobLauncherUnknownMatch;

typedef struct _GPtrArray {
    void** pdata;
    unsigned int len;
} GPtrArray;
extern GPtrArray* plugin_loader_search_providers;

static int vala_g_ptr_array_get_length(GPtrArray* self) {
    return (int)self->len;
}

extern char* bob_launcher_match_get_title(BobLauncherMatch* match);
extern int bob_launcher_plugin_base_get_enabled(BobLauncherSearchBase* plugin);
extern char* bob_launcher_plugin_base_to_string(BobLauncherSearchBase* plugin);
extern BobLauncherMatch* bob_launcher_plugin_base_make_match(BobLauncherSearchBase* plugin);

extern BobLauncherUnknownMatch* bob_launcher_unknown_match_new(const char* query);

static int compare_ascending(const void* a, const void* b) {
    int idx_a = *(const int*)a;
    int idx_b = *(const int*)b;

    BobLauncherSearchBase* plg_a = (BobLauncherSearchBase*)plugin_loader_search_providers->pdata[idx_a];
    BobLauncherSearchBase* plg_b = (BobLauncherSearchBase*)plugin_loader_search_providers->pdata[idx_b];

    char* title_a = bob_launcher_match_get_title((BobLauncherMatch*)plg_a);
    char* title_b = bob_launcher_match_get_title((BobLauncherMatch*)plg_b);

    int result = strcmp(title_a, title_b);

    free(title_a);
    free(title_b);

    return result;
}

static int compare_descending(const void* a, const void* b) {
    int idx_a = *(const int*)a;
    int idx_b = *(const int*)b;

    BobLauncherSearchBase* plg_a = (BobLauncherSearchBase*)plugin_loader_search_providers->pdata[idx_a];
    BobLauncherSearchBase* plg_b = (BobLauncherSearchBase*)plugin_loader_search_providers->pdata[idx_b];

    char* title_a = bob_launcher_match_get_title((BobLauncherMatch*)plg_a);
    char* title_b = bob_launcher_match_get_title((BobLauncherMatch*)plg_b);

    int result = strcmp(title_b, title_a);

    free(title_a);
    free(title_b);

    return result;
}

int data_sink_find_plugin_by_name(const char* query) {
    int length = vala_g_ptr_array_get_length(plugin_loader_search_providers);
    int* indices = malloc(length * sizeof(int));

    for (int i = 0; i < length; i++) {
        indices[i] = i;
    }

    qsort(indices, length, sizeof(int), compare_ascending);

    int enabled = 0;
    for (int i = 0; i < length; i++) {
        BobLauncherSearchBase* sp = (BobLauncherSearchBase*)plugin_loader_search_providers->pdata[indices[i]];
        char* plg_str = bob_launcher_match_get_title((BobLauncherMatch*)sp);
        if (strcmp(query, plg_str) == 0) {
            free(plg_str);
            free(indices);
            return enabled;
        }
        free(plg_str);
        enabled++;
    }

    free(indices);
    return -1;
}

static BobLauncherMatch* plugin_make_match_wrapper(void* data) {
    return bob_launcher_plugin_base_make_match((BobLauncherSearchBase*)data);
}

HashSet* data_sink_search_for_plugins(const char* query, int event_id) {
    HashSet* hsh = hashset_create(query, event_id);
    ResultContainer* rc = hashset_create_default_handle(hsh);
    needle_info* si = prepare_needle(query);

    int length = vala_g_ptr_array_get_length(plugin_loader_search_providers);
    int* indices = malloc(length * sizeof(int));

    for (int i = 0; i < length; i++) {
        indices[i] = i;
    }

    qsort(indices, length, sizeof(int), compare_descending);

    for (int i = 0; i < length; i++) {
        BobLauncherSearchBase* plg = (BobLauncherSearchBase*)plugin_loader_search_providers->pdata[indices[i]];

        char* title = bob_launcher_match_get_title((BobLauncherMatch*)plg);
        if (!query_has_match(si, title)) {
            free(title);
            continue;
        }

        int16_t score = match_score(si, title);
        free(title);

        result_container_add_lazy_unique(rc, score, plugin_make_match_wrapper, plg, NULL);
    }

    free(indices);
    free_string_info(si);

    hashset_merge(hsh, rc);
    hashset_prepare(hsh);

    return hsh;
}

struct unknown_match_data {
    char* query;
};

static BobLauncherMatch* create_unknown_match(void* data) {
    struct unknown_match_data* umd = (struct unknown_match_data*)data;
    return (BobLauncherMatch*)bob_launcher_unknown_match_new(umd->query);
}

static void free_unknown_match_data(void* data) {
    struct unknown_match_data* umd = (struct unknown_match_data*)data;
    free(umd->query);
    free(umd);
}

HashSet* data_sink_search_for_targets(const char* query, BobLauncherMatch* a, int event_id) {
    HashSet* hsh = hashset_create(query, event_id);
    ResultContainer* rc = hashset_create_default_handle(hsh);

    struct unknown_match_data* umd = malloc(sizeof(struct unknown_match_data));
    umd->query = strdup(query);

    result_container_add_lazy_unique(rc, 100, create_unknown_match, umd, free_unknown_match_data);

    hashset_merge(hsh, rc);
    hashset_prepare(hsh);

    return hsh;
}
