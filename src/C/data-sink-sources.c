#include "data-sink-sources.h"
#include "bob-launcher.h"
#include "result-container.h"
#include "match.h"
#include <glib.h>
#include <stdatomic.h>
#include <thread-manager.h>
#include <glib-object.h>
#include "string-utils.h"

typedef int BobLauncherSearchingFor;

extern GPtrArray* plugin_loader_default_search_providers;
extern int* state_selected_indices;
extern HashSet** state_providers;

extern int events_ok(int event_id);
extern int thread_pool_num_cores(void);
extern int state_update_provider(BobLauncherSearchingFor what, HashSet* new_provider, int selected_index);
extern void state_update_layout(BobLauncherSearchingFor what);

#define SEARCHING_FOR_SOURCES 1

static atomic_long shard_search_time = 0;
static atomic_int misses = 0;
static atomic_long shard_search_max = 0;
static atomic_int shard_search_count = 0;

typedef struct {
    atomic_int refs;
    struct timespec start_time;
    bool reset_index;
    HashSet* hashset;
} SearchContext;

typedef struct {
    char* query;
    needle_info* needle;
    needle_info* needle_spaceless;

    SearchContext* ctx;
    BobLauncherSearchBase* plugin;
    int16_t bonus;
    atomic_int refs;
} PluginData;

static void plugin_data_unref(PluginData* pd) {
    if (pd && atomic_fetch_sub(&pd->refs, 1) == 1) {
        free_string_info(pd->needle);
        free_string_info(pd->needle_spaceless);
        free(pd->query);
        free(pd);
    }
}

bool update_ui_callback(void* data) {
    SearchContext* ctx = (SearchContext*)data;

    HashSet* hashset = ctx->hashset;

    int size = atomic_load(&hashset->size);
    if (size < 0) {
        atomic_fetch_add(&misses, 1);
        __builtin_ia32_pause();
        return true;
    }

    bool reset_index = ctx->reset_index;

    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    long total_elapsed = (end.tv_sec - ctx->start_time.tv_sec) * 1000000000L +
                         (end.tv_nsec - ctx->start_time.tv_nsec);


    long shard_time_total = atomic_exchange(&shard_search_time, 0);
    long shard_time_max = atomic_exchange(&shard_search_max, 0);
    int shard_count = atomic_exchange(&shard_search_count, 0);
    double shard_time_avg = shard_count > 0 ? (shard_time_total / (double)shard_count) / 1000000.0 : 0;
    int _misses = atomic_exchange(&misses, 0);

    printf("Shards: avg %.3f ms, max %.3f ms, total %.3f ms, wall: %.3f ms, misses: %d\n",
           shard_time_avg, shard_time_max / 1000000.0, shard_time_total / 1000000.0, total_elapsed / 1000000.0, _misses);

    int old = state_providers[SEARCHING_FOR_SOURCES]->size;
    int new_index = reset_index && old != size ? 0 :
                    state_selected_indices[SEARCHING_FOR_SOURCES];

    if (state_update_provider(SEARCHING_FOR_SOURCES, hashset, new_index)) {
        state_update_layout(SEARCHING_FOR_SOURCES);
    }

    free(ctx);
    return false;
}

static inline void callback_or_cleanup(SearchContext* ctx) {
    if (atomic_fetch_sub(&ctx->refs, 1) - 1 != 0) {
        return;
    }

    HashSet* set = ctx->hashset;

    if (events_ok(set->event_id)) {
        hashset_prepare_new_parallel(set);
        g_main_context_invoke_full(NULL, G_PRIORITY_LOW, (GSourceFunc)update_ui_callback, (void*)ctx, NULL);
    } else {
        hashset_destroy(set);
        free(ctx);
    }
}

static void search_func(void* user_data) {
    int* my_id_ptr = (int*)user_data;
    int shard = *my_id_ptr;

    PluginData* plugin_data = (PluginData*)(my_id_ptr - shard) - 1;
    HashSet* set = plugin_data->ctx->hashset;

    if (!events_ok(set->event_id)) {
        plugin_data_unref(plugin_data);
        return;
    }

    ResultContainer* rc = hashset_create_handle(set, plugin_data->query, plugin_data->bonus,
                                                 plugin_data->needle, plugin_data->needle_spaceless);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    bob_launcher_search_base_search_shard(plugin_data->plugin, rc, shard);

    clock_gettime(CLOCK_MONOTONIC, &end);
    long elapsed = (end.tv_sec - start.tv_sec) * 1000000000L + (end.tv_nsec - start.tv_nsec);
    atomic_fetch_add(&shard_search_time, elapsed);
    atomic_fetch_add(&shard_search_count, 1);

    // CAS loop for max
    long current_max = atomic_load(&shard_search_max);
    while (elapsed > current_max) {
        if (atomic_compare_exchange_weak(&shard_search_max, &current_max, elapsed))
            break;
    }

    if (events_ok(set->event_id)) {
        container_flush_items(set, rc);
        container_return_sheet(set, rc);
        callback_or_cleanup(plugin_data->ctx);
    }

    container_destroy(rc);
    plugin_data_unref(plugin_data);
}

static void search_plugin(BobLauncherSearchBase* sp, SearchContext* ctx, const char* query) {
    size_t shard_count = bob_launcher_search_base_get_shard_count(sp);
    PluginData* plugin_data = aligned_alloc(64, sizeof(PluginData) + shard_count * sizeof(int));
    if (!plugin_data) return;

    plugin_data->plugin = sp;
    plugin_data->ctx = ctx;

    plugin_data->needle = prepare_needle(query);
    plugin_data->query = strdup(query);

    char* query_spaceless = replace(query, " ", "");
    plugin_data->needle_spaceless = prepare_needle(query_spaceless);
    free(query_spaceless);

    plugin_data->bonus = 0;
    atomic_init(&plugin_data->refs, shard_count);

    plugin_data->bonus = bob_launcher_plugin_base_get_bonus ((BobLauncherPluginBase*) sp);

    atomic_fetch_add(&ctx->refs, shard_count);

    int* worker_ids = (int*)(plugin_data + 1);

    for (size_t i = 0; i < shard_count; i++) {
        worker_ids[i] = i;
        thread_pool_run(search_func, &worker_ids[i], NULL);
    }
}

void data_sink_sources_execute_search(const char* query, BobLauncherSearchBase* selected_plg, int event_id, bool reset_index) {
    SearchContext* ctx = aligned_alloc(64, sizeof(SearchContext));
    if (ctx == NULL) return;
    atomic_init(&ctx->refs, 1);
    clock_gettime(CLOCK_MONOTONIC, &ctx->start_time);

    ctx->reset_index = reset_index;
    ctx->hashset = hashset_create(event_id);

    if (selected_plg) {
        GRegex* regex = bob_launcher_search_base_get_compiled_regex(selected_plg);
        int end_pos = 0;
        if (g_regex_get_capture_count(regex) > 0) {
            GMatchInfo *match_info;
            g_regex_match(regex, query, 0, &match_info);
            g_match_info_fetch_pos(match_info, 1, NULL, &end_pos);
            if (end_pos < 0) end_pos = 0;
            g_match_info_free(match_info);
        }

        search_plugin(selected_plg, ctx, query + end_pos);
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
                search_plugin(sp, ctx, query + end_pos);
                g_match_info_free(match_info);
            }
        }
    }
    callback_or_cleanup(ctx);
}
