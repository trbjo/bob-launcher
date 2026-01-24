#include "data-sink-sources.h"
#include "bob-launcher.h"
#include "result-container.h"
#include "match.h"

#include <glib.h>
#include <stdatomic.h>
#include <thread-manager.h>
#include <glib-object.h>
#include "string-utils.h"
#include <time.h>
#include <stdio.h>
#include <time.h>

static struct timespec bench_start, bench_end;

typedef int BobLauncherSearchingFor;

extern GPtrArray* plugin_loader_default_search_providers;
extern int* state_selected_indices;
extern HashSet** state_providers;

extern int thread_pool_num_cores(void);
extern int state_update_provider(BobLauncherSearchingFor what, HashSet* new_provider, int selected_index);
extern void state_update_layout(BobLauncherSearchingFor what);
extern char* bob_launcher_match_get_title(BobLauncherMatch* match);

#define SEARCHING_FOR_SOURCES 1

typedef struct {
    atomic_int workers;
    HashSet* set;
} SearchContext;

typedef struct {
    HashSet* set;
    int merge_id;
} MergeTask;

typedef struct {
    char* query;
    needle_info* needle;
    needle_info* needle_spaceless;
    atomic_int refs;
} SharedNeedle;

typedef struct {
    SharedNeedle* shared_needle;
    SearchContext* ctx;

    BobLauncherSearchBase* plugin;
    int16_t bonus;
    atomic_int refs;
} PluginData;

typedef struct {
    BobLauncherSearchBase* sp;
    size_t shard_count;
    SharedNeedle* needle;
} SearchPlugin;

static bool update_ui_callback(void* data) {
    HashSet* set = (HashSet*)data;
    int size = atomic_load_explicit(&set->size, memory_order_acquire);

    bool reset_index = true;

    int old = state_providers[SEARCHING_FOR_SOURCES]->size;
    int new_index = (reset_index && old != size) ? 0 :
                    state_selected_indices[SEARCHING_FOR_SOURCES];

    struct timespec draw_start;
    clock_gettime(CLOCK_MONOTONIC, &draw_start);

    if (state_update_provider(SEARCHING_FOR_SOURCES, set, new_index)) {
        state_update_layout(SEARCHING_FOR_SOURCES);
    }

    if (size) {
        clock_gettime(CLOCK_MONOTONIC, &bench_end);
        double draw_time = (bench_end.tv_sec - draw_start.tv_sec) * 1000 +
                           (bench_end.tv_nsec - draw_start.tv_nsec) / 1e6;

        double elapsed = (bench_end.tv_sec - bench_start.tv_sec) + (bench_end.tv_nsec - bench_start.tv_nsec) / 1e9;
        printf("%d entries, %.3f ms, draw_time: %.3f ms\n", size, elapsed * 1000, draw_time);
    }

    return false;
}

static inline void merge_hashset_parallel_wrapper(void* user_data) {
    const MergeTask* task = (MergeTask*)user_data;
    if (merge_hashset_parallel(task->set, task->merge_id)) {
        g_main_context_invoke_full(NULL, G_PRIORITY_HIGH,
                                   (GSourceFunc)update_ui_callback, task->set, NULL);
    }
}

static SharedNeedle* create_shared_needle(const char* query) {
    SharedNeedle* sn = malloc(sizeof(SharedNeedle));
    sn->query = strdup(query);
    sn->needle = prepare_needle(query);

    char* query_spaceless = replace(query, " ", "");
    sn->needle_spaceless = prepare_needle(query_spaceless);
    free(query_spaceless);

    atomic_init(&sn->refs, 0);
    return sn;
}

static void shared_needle_unref(SharedNeedle* sn) {
    if (atomic_fetch_sub(&sn->refs, 1) == 1) {
        free_string_info(sn->needle);
        free_string_info(sn->needle_spaceless);
        free(sn->query);
        free(sn);
    }
}

static void search_func(void* user_data) {
    int* my_id_ptr = (int*)user_data;
    int shard = *my_id_ptr;

    PluginData* plugin_data = (PluginData*)((char*)my_id_ptr - shard * sizeof(int) - sizeof(PluginData));
    HashSet* set = plugin_data->ctx->set;

    if (!events_ok(set->event_id))
        return;

    SharedNeedle* sn = plugin_data->shared_needle;
    ResultContainer* rc = hashset_create_handle(set, sn->query, plugin_data->bonus,
                                                 sn->needle, sn->needle_spaceless);
    bob_launcher_search_base_search_shard(plugin_data->plugin, rc, shard);

    container_flush_items(rc);
    container_return_sheet(set, rc);
    container_destroy(rc);
}

static void finalize_search(SearchContext* ctx) {
    if (atomic_fetch_sub(&ctx->workers, 1) != 1) return;

    if (events_ok(ctx->set->event_id)) {
        for (int i = 0; i < ctx->set->merge_workers; i++) {
            MergeTask* task = malloc(sizeof(MergeTask));
            task->set = ctx->set;
            task->merge_id = i;
            thread_pool_run(merge_hashset_parallel_wrapper, task, free);
        }
    } else {
        hashset_destroy(ctx->set);
    }

    free(ctx);
}

static void plugin_data_unref(void* user_data) {
    int* my_id_ptr = (int*)user_data;
    int shard = *my_id_ptr;

    PluginData* pd = (PluginData*)((char*)my_id_ptr - shard * sizeof(int) - sizeof(PluginData));

    finalize_search(pd->ctx);

    shared_needle_unref(pd->shared_needle);

    if (atomic_fetch_sub(&pd->refs, 1) == 1) {
        free(pd);
    }
}

static void search_plugin(BobLauncherSearchBase* sp, SearchContext* ctx, SharedNeedle* needle, size_t shard_count) {
    PluginData* plugin_data = aligned_alloc(64, sizeof(PluginData) + shard_count * sizeof(int));
    if (!plugin_data) return;

    plugin_data->plugin = sp;
    plugin_data->ctx = ctx;
    plugin_data->shared_needle = needle;
    plugin_data->bonus = bob_launcher_plugin_base_get_bonus((BobLauncherPluginBase*)sp);
    atomic_init(&plugin_data->refs, shard_count);

    atomic_fetch_add(&needle->refs, shard_count);

    int* worker_ids = (int*)(plugin_data + 1);

    for (size_t i = 0; i < shard_count; i++) {
        worker_ids[i] = (int)i;
        thread_pool_run(search_func, &worker_ids[i], plugin_data_unref);
    }
}

void data_sink_sources_execute_search(const char* query,
                                      BobLauncherSearchBase* selected_plg,
                                      const int event_id,
                                      const bool reset_index) {
    clock_gettime(CLOCK_MONOTONIC, &bench_start);
    SearchContext* ctx = aligned_alloc(64, sizeof(SearchContext));
    if (ctx == NULL) return;
    ctx->set = hashset_create(event_id);

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

        size_t shard_count = bob_launcher_search_base_get_shard_count(selected_plg);
        ctx->set->merge_workers = MIN(shard_count, hashset_merge_threads);
        atomic_init(&ctx->workers, shard_count);

        SharedNeedle* needle = create_shared_needle(query + end_pos);
        search_plugin(selected_plg, ctx, needle, shard_count);
    } else {
        SearchPlugin plugins[plugin_loader_default_search_providers->len];
        int counter = 0;
        int total_shards = 0;

        int query_len = strlen(query);
        SharedNeedle** needles_by_offset = calloc(query_len + 1, sizeof(SharedNeedle*));

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

                if (!needles_by_offset[end_pos])
                    needles_by_offset[end_pos] = create_shared_needle(query + end_pos);

                size_t shard_count = bob_launcher_search_base_get_shard_count(sp);
                total_shards += shard_count;

                plugins[counter++] = (SearchPlugin){sp, shard_count, needles_by_offset[end_pos]};
                g_match_info_free(match_info);
            }
        }

        atomic_init(&ctx->workers, total_shards);
        ctx->set->merge_workers = MIN(total_shards, hashset_merge_threads);
        for (size_t i = 0; i < counter; i++) {
            SearchPlugin plg = plugins[i];
            search_plugin(plg.sp, ctx, plg.needle, plg.shard_count);
        }

        free(needles_by_offset);
    }
}
