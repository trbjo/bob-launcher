#include "data-sink-sources.h"
#include "bob-launcher.h"
#include "result-container.h"
#include <glib.h>
#include <thread-manager.h>
#include <glib-object.h>

typedef int BobLauncherSearchingFor;

extern GPtrArray* plugin_loader_default_search_providers;
extern int* state_selected_indices;
extern HashSet** state_providers;

extern int events_ok(int event_id);
extern int thread_pool_num_cores(void);
extern int state_update_provider(BobLauncherSearchingFor what, HashSet* new_provider, int selected_index);
extern void state_update_layout(BobLauncherSearchingFor what);

#define SEARCHING_FOR_SOURCES 1
#define CACHE_ALIGNED __attribute__((aligned(CACHE_LINE_SIZE)))
#define ALWAYS_INLINE inline __attribute__((always_inline))

#define CACHE_LINE_SIZE 64

typedef struct {
    atomic_int* refs;

    BobLauncherSearchBase* plugin;
    unsigned int shard;
    char* query;
    int16_t bonus;

    HashSet* hashset;
} CACHE_ALIGNED WorkerData;

static bool update_ui_callback(void* data) {
    HashSet* hashset = (HashSet*)((uint64_t)data & ((1ULL << 63) - 1));
    bool reset_index = ((uint64_t)data >> 63) & 1;

    int old = state_providers[SEARCHING_FOR_SOURCES]->size;
    int new_index = reset_index && old != hashset->size ? 0 :
                    state_selected_indices[SEARCHING_FOR_SOURCES];

    if (state_update_provider(SEARCHING_FOR_SOURCES, hashset, new_index)) {
        state_update_layout(SEARCHING_FOR_SOURCES);
    }

    return false;
}

static inline void callback_or_cleanup(HashSet* set, atomic_int* refs, bool reset_index) {
    if (atomic_fetch_sub(refs, 1) == 1) {
        if (events_ok(set->event_id)) {
            hashset_prepare(set);
            uint64_t encoded_hashset = ((uint64_t)reset_index << 63) + (uint64_t)set;
            /* Schedule UI update on main thread if we're the last worker */
            g_main_context_invoke_full(NULL, G_PRIORITY_HIGH, (GSourceFunc)update_ui_callback, (void*)encoded_hashset, NULL);
        } else {
            hashset_destroy(set);
        }
        free(refs);
    }
}

static void worker_finalize_reset(void* user_data) {
    WorkerData* worker_data = (WorkerData*)user_data;
    callback_or_cleanup(worker_data->hashset, worker_data->refs, true);
    free(worker_data->query);
    free(worker_data);
}

static void worker_finalize(void* user_data) {
    WorkerData* worker_data = (WorkerData*)user_data;
    callback_or_cleanup(worker_data->hashset, worker_data->refs, false);
    free(worker_data->query);
    free(worker_data);
}

static ALWAYS_INLINE ResultContainer* _search_worker_thread(void* user_data) {
    WorkerData* worker_data = (WorkerData*)user_data;
    HashSet* set = worker_data->hashset;

    if (!events_ok(set->event_id)) return NULL;
    ResultContainer* rc = hashset_create_handle(set, worker_data->query, worker_data->bonus);

    bob_launcher_search_base_search_shard(worker_data->plugin,
                                          rc,
                                          worker_data->shard);

    if (!events_ok(set->event_id)) {
        container_destroy(rc);
        return NULL;
    }

    return rc;
}

static void search_prefer_insertion(void* user_data) {
    ResultContainer* rc = _search_worker_thread(user_data);
    if (rc != NULL) hashset_merge_prefer_insertion(((WorkerData*)user_data)->hashset, rc);
}

static void search_prefer_hash(void* user_data) {
    ResultContainer* rc = _search_worker_thread(user_data);
    if (rc != NULL) hashset_merge_prefer_hash(((WorkerData*)user_data)->hashset, rc);
}

static inline void search_plugin(BobLauncherSearchBase* sp, HashSet* hashset, const char* query, atomic_int* refs, void* search_func, void* finalize_func) {
    size_t shard_count = bob_launcher_search_base_get_shard_count(sp);
    int16_t bonus = bob_launcher_plugin_base_get_bonus ((BobLauncherPluginBase*) sp);

    atomic_fetch_add(refs, shard_count);

    for (size_t shard = 0; shard < shard_count; shard++) {

        WorkerData* worker_data = aligned_alloc(64, sizeof(WorkerData));

        if (worker_data == NULL) {
            atomic_fetch_sub(refs, 1);
            continue;
        }

        worker_data->hashset = hashset;
        worker_data->shard = shard;
        worker_data->plugin = sp;
        worker_data->bonus = bonus;
        worker_data->query = strdup(query);
        worker_data->refs = refs;

        thread_pool_run(search_func, worker_data, finalize_func);
    }
}

void data_sink_sources_execute_search(const char* query, BobLauncherSearchBase* selected_plg, int event_id, bool reset_index) {
    atomic_int* refs = aligned_alloc(64, 64);
    if (refs == NULL) return;
    atomic_init(refs, 1);

    HashSet* hashset = hashset_create(event_id);
    void* finalize_func = reset_index ? worker_finalize_reset : worker_finalize;

    if (selected_plg) {
        void* search_func = bob_launcher_search_base_get_prefer_insertion_order(selected_plg) ?
            search_prefer_insertion :
            search_prefer_hash;

        GRegex* regex = bob_launcher_search_base_get_compiled_regex(selected_plg);
        int end_pos = 0;
        if (g_regex_get_capture_count(regex) > 0) {
            GMatchInfo *match_info;
            g_regex_match(regex, query, 0, &match_info);
            g_match_info_fetch_pos(match_info, 1, NULL, &end_pos);
            if (end_pos < 0) end_pos = 0;
            g_match_info_free(match_info);
        }

        search_plugin(selected_plg, hashset, query + end_pos, refs, search_func, finalize_func);
    } else {
        for (size_t i = 0; i < plugin_loader_default_search_providers->len; i++) {
            BobLauncherSearchBase* sp = plugin_loader_default_search_providers->pdata[i];

            GRegex* regex = bob_launcher_search_base_get_compiled_regex(sp);
            GMatchInfo *match_info;

            if (g_regex_match(regex, query, 0, &match_info)) {
                int end_pos = 0;
                if (g_regex_get_capture_count(regex) > 0) {
                    g_match_info_fetch_pos(match_info, 1, NULL, &end_pos);
                    if (end_pos < 0) end_pos = 0;
                }
                g_match_info_free(match_info);
                search_plugin(sp, hashset, query + end_pos, refs, search_prefer_hash, finalize_func);
            }
        }
    }

    callback_or_cleanup(hashset, refs, reset_index);
}
