#include "data-sink-sources.h"
#include "bob-launcher.h"
#include "result-container.h"
#include "match.h"
#include <glib.h>
#include <stdatomic.h>
#include <thread-manager.h>
#include <glib-object.h>
#include "string-utils.h"
#include <sys/syscall.h>
#include <linux/futex.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>

typedef int BobLauncherSearchingFor;

extern GPtrArray* plugin_loader_default_search_providers;
extern int* state_selected_indices;
extern HashSet** state_providers;

extern int thread_pool_num_cores(void);
extern int state_update_provider(BobLauncherSearchingFor what, HashSet* new_provider, int selected_index);
extern void state_update_layout(BobLauncherSearchingFor what);
extern char* bob_launcher_match_get_title(BobLauncherMatch* match);

#define SEARCHING_FOR_SOURCES 1

typedef struct SearchContext SearchContext;
typedef struct PluginData PluginData;

bool update_ui_callback(void* data);

#define READY 1
#define WAITING 0
#define NO_MERGE -1

typedef struct SearchContext {
    atomic_int merge_workers;
    atomic_int workers;
    atomic_int ready;
    int event_id;
    HashSet* set;
} SearchContext;

typedef struct {
    SearchContext* ctx;
    int merge_id;
} MergeTask;

typedef struct PluginData {
    char* query;
    needle_info* needle;
    needle_info* needle_spaceless;
    SearchContext* ctx;

    BobLauncherSearchBase* plugin;
    int16_t bonus;
    atomic_int refs;
} PluginData;

typedef struct {
    BobLauncherSearchBase* sp;
    size_t shard_count;
    char* query;
} SearchPlugin;

bool update_ui_callback(void* data) {
    HashSet* set = (HashSet*)data;

    int size = atomic_load(&set->size);

    if (size < 0) {
        if (!events_ok(set->event_id)) {
            thread_pool_run((TaskFunc)hashset_destroy, set, NULL);
            return false;
        }
        __builtin_ia32_pause();
        return true;
    }

    bool reset_index = true;

    int old = state_providers[SEARCHING_FOR_SOURCES]->size;
    int new_index = (reset_index && old != size) ? 0 :
                    state_selected_indices[SEARCHING_FOR_SOURCES];

    if (state_update_provider(SEARCHING_FOR_SOURCES, set, new_index)) {
        state_update_layout(SEARCHING_FOR_SOURCES);
    }

    return false;
}

void merge_hashset_parallel_wrapper(void* user_data) {
    MergeTask* task = (MergeTask*)user_data;
    SearchContext* ctx = task->ctx;

    merge_hashset_parallel(task->ctx->set, task->merge_id);

    if (atomic_fetch_sub(&ctx->merge_workers, 1) == 1) {
        free(ctx);
    }
}

static void search_func(void* user_data) {
    int* my_id_ptr = (int*)user_data;
    int shard = *my_id_ptr;

    PluginData* plugin_data = (PluginData*)((char*)my_id_ptr - shard * sizeof(int) - sizeof(PluginData));
    HashSet* set = plugin_data->ctx->set;

    if (!events_ok(set->event_id))
        return;

    ResultContainer* rc = hashset_create_handle(set, plugin_data->query, plugin_data->bonus,
                                                 plugin_data->needle, plugin_data->needle_spaceless);
    bob_launcher_search_base_search_shard(plugin_data->plugin, rc, shard);

    container_flush_items(set, rc);
    container_return_sheet(set, rc);
    container_destroy(rc);
}

static void finalize_search(SearchContext* ctx) {
    HashSet* set = ctx->set;

    int remaining = atomic_fetch_sub(&ctx->workers, 1) - 1;
    if (remaining >= MERGE_THREADS) return;

    int merge_id = atomic_fetch_add(&ctx->merge_workers, 1);
    int status = WAITING;

    if (remaining == 0) {
        if (events_ok(set->event_id)) {
            status = READY;

            // for plugins that don't have enough shards, simply spin up the missing mergers
            int current = merge_id;
            while (current < MERGE_THREADS) {
                if (atomic_compare_exchange_weak(&ctx->merge_workers, &current, current + 1)) {
                    MergeTask* task = malloc(sizeof(MergeTask));
                    task->ctx = ctx;
                    task->merge_id = current;
                    thread_pool_run(merge_hashset_parallel_wrapper, task, free);
                    current += 1;
                }
            }
            atomic_store(&ctx->ready, READY);

            g_main_context_invoke_full(NULL, G_PRIORITY_LOW,
                                       (GSourceFunc)update_ui_callback, set, NULL);
        } else {
            status = NO_MERGE;
            atomic_store(&ctx->ready, NO_MERGE);
            hashset_destroy(set);
        }
    } else {
        volatile int dummy = 0;

        while ((status = atomic_load(&ctx->ready)) == WAITING) {
            dummy += (merge_id + 1) << merge_id;
            __builtin_ia32_pause();
        }
    }

    if (status == READY)
        merge_hashset_parallel(set, merge_id);


    if (atomic_fetch_sub(&ctx->merge_workers, 1) == 1) {
        free(ctx);
    }
}

static void plugin_data_unref(void* user_data) {
    int* my_id_ptr = (int*)user_data;
    int shard = *my_id_ptr;

    PluginData* pd = (PluginData*)((char*)my_id_ptr - shard * sizeof(int) - sizeof(PluginData));

    SearchContext* ctx = pd->ctx;

    if (pd && atomic_fetch_sub(&pd->refs, 1) == 1) {
        free_string_info(pd->needle);
        free_string_info(pd->needle_spaceless);
        free(pd->query);
        free(pd);
    }

    finalize_search(ctx);
}

static void search_plugin(BobLauncherSearchBase* sp, SearchContext* ctx, const char* query, size_t shard_count) {
    PluginData* plugin_data = aligned_alloc(64, sizeof(PluginData) + shard_count * sizeof(int));
    if (!plugin_data) return;

    plugin_data->plugin = sp;
    plugin_data->ctx = ctx;

    plugin_data->needle = prepare_needle(query);
    plugin_data->query = strdup(query);

    char* query_spaceless = replace(query, " ", "");
    plugin_data->needle_spaceless = prepare_needle(query_spaceless);
    free(query_spaceless);

    plugin_data->bonus = bob_launcher_plugin_base_get_bonus((BobLauncherPluginBase*)sp);
    atomic_init(&plugin_data->refs, shard_count);

    int* worker_ids = (int*)(plugin_data + 1);

    for (size_t i = 0; i < shard_count; i++) {
        worker_ids[i] = (int)i;
        thread_pool_run(search_func, &worker_ids[i], plugin_data_unref);
    }
}

void data_sink_sources_execute_search(const char* query, BobLauncherSearchBase* selected_plg,
                                       int event_id, bool reset_index) {
    SearchContext* ctx = aligned_alloc(64, sizeof(SearchContext));
    if (ctx == NULL) return;
    ctx->event_id = event_id;
    atomic_init(&ctx->ready, WAITING);
    atomic_init(&ctx->merge_workers, 0);
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
        atomic_init(&ctx->workers, shard_count);
        search_plugin(selected_plg, ctx, query + end_pos, shard_count);
    } else {
        SearchPlugin* plugins = malloc(plugin_loader_default_search_providers->len * sizeof(SearchPlugin));
        int counter = 0;
        int total_shards = 0;
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
                size_t shard_count = bob_launcher_search_base_get_shard_count(sp);
                total_shards += shard_count;

                plugins[counter++] = (SearchPlugin){sp, shard_count, query + end_pos};
                g_match_info_free(match_info);
            }
        }

        atomic_init(&ctx->workers, total_shards);
        for (size_t i = 0; i < counter; i++) {
            SearchPlugin plg = plugins[i];
            search_plugin(plg.sp, ctx, plg.query, plg.shard_count);
        }
        free(plugins);
    }
}
