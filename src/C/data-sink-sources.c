#include "data-sink-sources.h"
#include "bob-launcher.h"
#include "result-container.h"
#include <glib.h>
#include <thread-manager.h>
#include <glib-object.h>
#include <gtk/gtk.h>

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
    _Atomic int remaining;
    char _padding1[CACHE_LINE_SIZE - sizeof(_Atomic int)];

    bool reset_index;
    uintptr_t* combined_data; // Packed plugin pointers and shard indices
    HashSet* hashset;
} CACHE_ALIGNED WorkerData;

static bool update_ui_callback(void* data) {
    HashSet* hashset = (HashSet*)((uint64_t)data & ((1ULL << 63) - 1));
    bool reset_index = ((uint64_t)data >> 63) & 1;

    int old = state_providers[SEARCHING_FOR_SOURCES]->size;
    int new_index = reset_index && old != hashset->size ? 0 :
                    state_selected_indices[SEARCHING_FOR_SOURCES];

    while (atomic_load(&hashset->size) == -1) _mm_pause();

    if (state_update_provider(SEARCHING_FOR_SOURCES, hashset, new_index)) {
        state_update_layout(SEARCHING_FOR_SOURCES);
    }

    return false;
}

static void search_worker_thread(void* user_data) {
    WorkerData* worker_data = (WorkerData*)user_data;
    _Atomic int* remaining = &worker_data->remaining;

    HashSet* hashset = worker_data->hashset;
    ResultContainer* hashset_handle = hashset_create_handle(hashset);

    unsigned int event_id = (unsigned int)hashset->event_id;
    uintptr_t* combined_data = worker_data->combined_data;

    int j;
    while ((j = atomic_dec(remaining) - 1) >= 0 && events_ok(event_id)) {
        uintptr_t packed = combined_data[j];
        unsigned int shard = (unsigned int)(packed >> 48);
        BobLauncherSearchBase* plugin = (BobLauncherSearchBase*)(packed & 0xFFFFFFFFFFFFULL);
        bob_launcher_search_base_search_shard(plugin, hashset_handle, shard);
    }

    if (hashset_complete_merge(hashset, hashset_handle)) {
        /* Schedule UI update on main thread if we're the last worker */
        uint64_t encoded_hashset = ((uint64_t)worker_data->reset_index << 63) + (uint64_t)hashset;
        g_main_context_invoke_full(NULL, G_PRIORITY_HIGH, (GSourceFunc)update_ui_callback, (void*)encoded_hashset, NULL);
        free(worker_data->combined_data);
        free(worker_data);
    }
}

static int largest_size = 0;

void data_sink_sources_execute_search(const char* query, BobLauncherSearchBase* selected_plg, int event_id, bool reset_index) {
    if (query == NULL) return;
    WorkerData* worker_data = malloc(sizeof(WorkerData));
    if (worker_data == NULL) return;

    size_t capacity = selected_plg != NULL ? bob_launcher_search_base_get_shard_count(selected_plg) : largest_size;
    worker_data->combined_data = malloc(capacity * sizeof(uintptr_t));
    if (worker_data->combined_data == NULL) {
        free(worker_data);
        return;
    }

    worker_data->reset_index = reset_index;
    worker_data->remaining = 0;

    // Single pass: collect and pack plugin pointers and shard indices
    if (selected_plg != NULL) {
        for (unsigned int shard = 0; shard < capacity; shard++) {
            worker_data->combined_data[worker_data->remaining++] = ((uintptr_t)shard << 48) | ((uintptr_t)selected_plg & 0xFFFFFFFFFFFFULL);
        }
    } else {
        unsigned int query_length = strlen(query);
        for (size_t i = 0; i < plugin_loader_search_providers->len; i++) {
            BobLauncherSearchBase* sp = (BobLauncherSearchBase*)plugin_loader_search_providers->pdata[i];

            if (bob_launcher_plugin_base_is_enabled((BobLauncherPluginBase*)sp) &&
                bob_launcher_search_base_get_enabled_in_default_search(sp) &&
                bob_launcher_search_base_get_char_threshold(sp) <= query_length) {

                unsigned int shard_count = bob_launcher_search_base_get_shard_count(sp);

                if (worker_data->remaining + shard_count > capacity) {
                    // Double the capacity or increase to fit the new shards, whichever is larger
                    size_t new_capacity = MAX(capacity * 2, worker_data->remaining + shard_count);
                    uintptr_t* new_data = realloc(worker_data->combined_data, new_capacity * sizeof(uintptr_t));
                    if (new_data == NULL) {
                        free(worker_data->combined_data);
                        free(worker_data);
                        return;
                    }
                    worker_data->combined_data = new_data;
                    capacity = new_capacity;
                }

                for (unsigned int shard = 0; shard < shard_count; shard++) {
                    worker_data->combined_data[worker_data->remaining++] = ((uintptr_t)shard << 48) | ((uintptr_t)sp & 0xFFFFFFFFFFFFULL);
                }
            }
        }
    }

    if (largest_size < worker_data->remaining) {
        largest_size = worker_data->remaining;
    }

    int workers = MIN(thread_pool_num_cores(), worker_data->remaining);
    worker_data->hashset = hashset_create(query, event_id, workers);

    for (int i = 0; i < workers; i++) {
        thread_pool_run(search_worker_thread, worker_data, NULL);
    }
}
