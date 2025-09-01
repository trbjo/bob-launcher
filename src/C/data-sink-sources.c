#include "data-sink-sources.h"
#include "bob-launcher.h"
#include "result-container.h"
#include <glib.h>
#include <thread-manager.h>
#include <glib-object.h>

typedef int BobLauncherSearchingFor;

extern GPtrArray* plugin_loader_search_providers;
extern int* state_selected_indices;
extern HashSet** state_providers;

extern int events_ok(unsigned int event_id);
extern int thread_pool_num_cores(void);
extern int state_update_provider(BobLauncherSearchingFor what, HashSet* new_provider, int selected_index);
extern void state_update_layout(BobLauncherSearchingFor what);

#define SEARCHING_FOR_SOURCES 1
#define CACHE_ALIGNED __attribute__((aligned(CACHE_LINE_SIZE)))
#define CACHE_LINE_SIZE 64

typedef struct {
    atomic_int* refs;

    BobLauncherSearchBase* plugin;
    unsigned int shard;

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
    free(worker_data);
}

static void worker_finalize(void* user_data) {
    WorkerData* worker_data = (WorkerData*)user_data;
    callback_or_cleanup(worker_data->hashset, worker_data->refs, false);
    free(worker_data);
}

static void search_worker_thread(void* user_data) {
    WorkerData* worker_data = (WorkerData*)user_data;
    HashSet* set = worker_data->hashset;

    if (!events_ok(set->event_id)) return;
    ResultContainer* rc = hashset_create_handle(set);

    bob_launcher_search_base_search_shard(worker_data->plugin,
                                          rc,
                                          worker_data->shard);

    if (events_ok(set->event_id)) {
        hashset_merge(set, rc);
    } else {
        container_destroy(rc);
    }
}

static inline void search_plugin(BobLauncherSearchBase* sp, HashSet* hashset, atomic_int* refs, void* finalize_func) {
    size_t shard_count = bob_launcher_search_base_get_shard_count(sp);

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
        worker_data->refs = refs;

        thread_pool_run(search_worker_thread, worker_data, finalize_func);
    }
}

void data_sink_sources_execute_search(const char* query, BobLauncherSearchBase* selected_plg, int event_id, bool reset_index) {
    atomic_int* refs = aligned_alloc(64, 64);
    if (refs == NULL) return;
    atomic_init(refs, 1);

    HashSet* hashset = hashset_create(query, event_id);
    void* finalize_func = reset_index ? worker_finalize_reset : worker_finalize;

    if (selected_plg) {
        search_plugin(selected_plg, hashset, refs, finalize_func);
    } else {
        for (size_t i = 0; i < plugin_loader_search_providers->len; i++) {
            BobLauncherSearchBase* sp = (BobLauncherSearchBase*)plugin_loader_search_providers->pdata[i];

            if (bob_launcher_plugin_base_is_enabled((BobLauncherPluginBase*)sp) &&
                bob_launcher_search_base_get_enabled_in_default_search(sp) &&
                g_regex_match(bob_launcher_search_base_get_compiled_regex(sp), query, 0, NULL )) {
                search_plugin(sp, hashset, refs, finalize_func);
            }
        }
    }

    callback_or_cleanup(hashset, refs, reset_index);
}
