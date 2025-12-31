#include <stdatomic.h>
#include <unistd.h>
#include <glib-object.h>
#include <stdio.h>
#include <immintrin.h>
#include "string-utils.h"
#include <thread-manager.h>

#include <stdlib.h>
#include <string.h>
#include "hashset.h"

#define MAX_POOLED_HASHSETS 8
#define INITIAL_UNFINISHED -1

extern int events_ok(int event_id);

static HashSet* hashset_pool[MAX_POOLED_HASHSETS];
static _Atomic(HashSet**) pool_pos = hashset_pool;

static inline HashSet* grab_hashset_from_pool(void) {
    HashSet** current_pos;
    __atomic_load(&pool_pos, &current_pos, __ATOMIC_RELAXED);
    while (current_pos > hashset_pool) {
        HashSet** next_pos = current_pos - 1;
        if (atomic_compare_exchange_strong(&pool_pos, &current_pos, next_pos)) {
            return *next_pos;
        }
        __builtin_ia32_pause();
    }
    return NULL;
}

static inline bool return_hashset_to_pool(HashSet* set) {
    HashSet** current_pos;
    __atomic_load(&pool_pos, &current_pos, __ATOMIC_RELAXED);
    while (current_pos < hashset_pool + MAX_POOLED_HASHSETS) {
        HashSet** next_pos = current_pos + 1;
        if (atomic_compare_exchange_strong(&pool_pos, &current_pos, next_pos)) {
            *current_pos = set;
            return true;
        }
        __builtin_ia32_pause();
    }
    return false;
}

static void reset_hashset(HashSet* set) {
    set->score_items = NULL;
    set->matches = NULL;

    size_t n = MAX_SHEETS * SHEET_SIZE;
    memset(set->combined, 0, n * sizeof(uint32_t) * 3);

    int num_sheets = atomic_load(&set->global_index_counter);
    for (int i = 0; i < num_sheets; i++) {
        if (set->sheet_pool[i]) {
            set->sheet_pool[i]->size = 0;
        }
    }

    atomic_store(&set->size, INITIAL_UNFINISHED);
    atomic_store(&set->hash_size, 0);
    atomic_store(&set->global_index_counter, 0);
    atomic_store(&set->write, 0);

    memset(set->unfinished_queue, 0, sizeof(set->unfinished_queue));
    atomic_store(&set->read, set->unfinished_queue);

    atomic_store(&set->bar[0].v, 0);
    atomic_store(&set->bar[1].v, 0);
}

static HashSet* hashset_reuse(int event_id) {
    HashSet* set = grab_hashset_from_pool();
    if (!set) return NULL;
    set->event_id = event_id;
    return set;
}

static HashSet* hashset_new(int event_id) {
    HashSet* set = malloc(sizeof(HashSet));
    if (!set) return NULL;

    size_t n = MAX_SHEETS * SHEET_SIZE;

    set->hash_items = aligned_alloc(64, n * sizeof(uint64_t));
    if (!set->hash_items) {
        free(set);
        return NULL;
    }

    set->combined = aligned_alloc(64, n * sizeof(uint32_t) * 3);
    if (!set->combined) {
        free(set->hash_items);
        free(set);
        return NULL;
    }

    set->sheet_pool = malloc(MAX_SHEETS * sizeof(ResultSheet*));
    if (!set->sheet_pool) {
        free(set->combined);
        free(set->hash_items);
        free(set);
        return NULL;
    }

    set->counts = aligned_alloc(CACHELINE, MERGE_THREADS * 256 * sizeof(uint16_t));

    if (!set->counts) {
        free(set->sheet_pool);
        free(set->combined);
        free(set->hash_items);
        free(set);
        return NULL;
    }

    set->score_items = NULL;
    set->matches = NULL;
    set->event_id = event_id;
    memset(set->unfinished_queue, 0, sizeof(set->unfinished_queue));

    atomic_init(&set->size, INITIAL_UNFINISHED);
    atomic_init(&set->hash_size, 0);
    atomic_init(&set->global_index_counter, 0);
    atomic_init(&set->write, 0);
    atomic_init(&set->read, set->unfinished_queue);
    atomic_init(&set->bar[0].v, 0);
    atomic_init(&set->bar[1].v, 0);
    return set;
}

HashSet* hashset_create(int event_id) {
    HashSet* set = hashset_reuse(event_id);
    if (set) return set;
    return hashset_new(event_id);
}

ResultContainer* hashset_create_handle(HashSet* hashset, char* query, int16_t bonus, needle_info* string_info, needle_info* string_info_spaceless) {
    ResultContainer* container = malloc(sizeof(ResultContainer));
    if (!container) return NULL;

    container->query = query;
    container->string_info = string_info;
    container->string_info_spaceless = string_info_spaceless;
    container->bonus = bonus;

    container->event_id = hashset->event_id;
    container->sheet_pool = hashset->sheet_pool;
    container->global_index_counter = &hashset->global_index_counter;
    container->read = &hashset->read;
    container->global_items_size = &hashset->hash_size;
    container->global_items = hashset->hash_items;

    container->current_sheet = NULL;
    container->match_mre_idx = 0;
    container->local_items_size = 0;

    container->local_items = malloc(SHEET_SIZE * sizeof(uint64_t));
    return container;
}

ResultContainer* hashset_create_default_handle(HashSet* hashset, char* query) {
    needle_info* string_info = prepare_needle(query);
    char* query_spaceless = replace(query, " ", "");
    needle_info* string_info_spaceless = prepare_needle(query_spaceless);
    free(query_spaceless);

    return hashset_create_handle(hashset, query, 0, string_info, string_info_spaceless);
}

static void radix_sort_hash_asc(uint64_t* arr, uint64_t* tmp, size_t n) {
    if (n <= 1) return;

    size_t count[256] = {0};

    for (size_t i = 0; i < n; i++)
        count[((arr[i] >> 32) & 0xFF)]++;
    for (int i = 1; i < 256; i++)
        count[i] += count[i-1];
    for (size_t i = n; i-- > 0;)
        tmp[--count[((arr[i] >> 32) & 0xFF)]] = arr[i];

    memset(count, 0, sizeof(count));

    for (size_t i = 0; i < n; i++)
        count[((tmp[i] >> 40) & 0xFF)]++;
    for (int i = 1; i < 256; i++)
        count[i] += count[i-1];
    for (size_t i = n; i-- > 0;)
        arr[--count[((tmp[i] >> 40) & 0xFF)]] = tmp[i];

    memset(count, 0, sizeof(count));

    for (size_t i = 0; i < n; i++)
        count[((arr[i] >> 48) & 0xFF)]++;
    for (int i = 1; i < 256; i++)
        count[i] += count[i-1];
    for (size_t i = n; i-- > 0;)
        tmp[--count[((arr[i] >> 48) & 0xFF)]] = arr[i];

    memset(count, 0, sizeof(count));

    for (size_t i = 0; i < n; i++)
        count[((tmp[i] >> 56) & 0xFF)]++;
    for (int i = 1; i < 256; i++)
        count[i] += count[i-1];
    for (size_t i = n; i-- > 0;)
        arr[--count[((tmp[i] >> 56) & 0xFF)]] = tmp[i];
}

static void radix_sort_score(uint64_t* src, uint32_t* tmp, uint32_t* dest, size_t n) {
    if (n == 0) return;

    if (n == 1) {
        dest[0] = (uint32_t)src[0];
        return;
    }

    size_t count[256] = {0};

    for (size_t i = 0; i < n; i++)
        count[255 - ((src[i] >> 16) & 0xFE)]++;
    for (int i = 1; i < 256; i++)
        count[i] += count[i-1];
    for (size_t i = n; i-- > 0;)
        tmp[--count[255 - ((src[i] >> 16) & 0xFE)]] = (uint32_t)src[i];

    memset(count, 0, sizeof(count));

    for (size_t i = 0; i < n; i++)
        count[255 - ((tmp[i] >> 24) & 0xFF)]++;
    for (int i = 1; i < 256; i++)
        count[i] += count[i-1];
    for (size_t i = n; i-- > 0;)
        dest[--count[255 - ((tmp[i] >> 24) & 0xFF)]] = tmp[i];
}

static inline bool barrier_check_last(HashSet* s, int* bi) {
    int idx = *bi;
    *bi ^= 1;
    return atomic_fetch_add(&s->bar[idx].v, 1) == MERGE_THREADS - 1;
}

static inline void barrier_spin(HashSet* s, int bi) {
    while (atomic_load(&s->bar[bi ^ 1].v) != 0) {
        _mm_pause();
    }
}

static inline void barrier_release(HashSet* s, int bi) {
    atomic_store(&s->bar[bi ^ 1].v, 0);
}

static inline void barrier_wait(HashSet* s, int* bi) {
    if (barrier_check_last(s, bi))
        barrier_release(s, *bi);
    else barrier_spin(s, *bi);
}

static inline void parallel_prefix_sum(HashSet* set, const int tid, uint32_t bucket[256], int* bi) {
    memcpy(set->counts[tid], (uint16_t*)bucket, sizeof(uint16_t) * 256);

    barrier_wait(set, bi);

    // Every thread computes its own offsets in parallel
    size_t bucket_base = 0;
    for (int b = 0; b < 256; b++) {
        for (int t = 0; t < tid; t++) {
            bucket_base += set->counts[t][b];
        }
        bucket[b] = bucket_base;
        for (int t = tid; t < MERGE_THREADS; t++) {
            bucket_base += set->counts[t][b];
        }
    }
}

void merge_hashset_parallel(HashSet* set, int tid) {
    uint32_t bucket[256] = {0};
    uint16_t* bucket16 = (uint16_t*)bucket;  // use first 512 bytes for counting

    const int64_t n = atomic_load(&set->hash_size);
    const int64_t chunk = (n + MERGE_THREADS - 1) / MERGE_THREADS;
    int64_t start = tid * chunk;
    int64_t end = (tid + 1) * chunk;
    if (end > n) end = n;

    int bi = 0;

    uint64_t* restrict tmp = (uint64_t*)set->combined;
    uint64_t* restrict hash_items = set->hash_items;

    for (int shift = 32; shift < 64; shift += 8) {
        const int odd = shift & 8;
        uint64_t* restrict src = odd ? tmp : hash_items;
        uint64_t* restrict dst = odd ? hash_items : tmp;

        for (uint32_t i = start; i < end; i++)
            bucket16[(src[i] >> shift) & 0xFF]++;

        parallel_prefix_sum(set, tid, bucket, &bi);

        for (uint32_t i = start; i < end; i++)
            dst[bucket[(src[i] >> shift) & 0xFF]++] = src[i];

        barrier_wait(set, &bi);
        memset(bucket, 0, sizeof(uint16_t) * 256);
    }

    int local_dups = 0;

    while (0 < start && start < n && (hash_items[start] >> 32) == (hash_items[start - 1] >> 32))
        start++;

    while (end < n && (hash_items[end] >> 32) == (hash_items[end - 1] >> 32))
        end++;

    for (uint32_t i = start + 1; i < end; i++) {
        const uint64_t a = hash_items[i - 1];
        const uint64_t b = hash_items[i];
        if ((a >> 32) == (b >> 32)) {
            hash_items[i - 1] &= ~0xFFFFFFFFULL; // set score to 0 so the items will appear last
            if (b < a) hash_items[i] = a; // move the highest scoring candidate forwards.
            local_dups++;
        }
    }

    for (uint32_t i = start; i < end; i++)
        bucket16[255 - ((hash_items[i] >> 16) & 0xFE)]++;

    parallel_prefix_sum(set, tid, bucket, &bi);

    uint32_t* restrict score_tmp = set->combined + n;

    for (uint32_t i = start; i < end; i++)
        score_tmp[bucket[255 - ((hash_items[i] >> 16) & 0xFE)]++] = (uint32_t)hash_items[i];

    barrier_wait(set, &bi);
    memset(bucket, 0, sizeof(uint16_t) * 256);

    for (uint32_t i = start; i < end; i++)
        bucket16[255 - ((score_tmp[i] >> 24) & 0xFF)]++;

    parallel_prefix_sum(set, tid, bucket, &bi);

    uint32_t* restrict dest = set->combined;

    for (uint32_t i = start; i < end; i++)
        dest[bucket[255 - ((score_tmp[i] >> 24) & 0xFF)]++] = score_tmp[i];

    if (barrier_check_last(set, &bi)) {
        set->score_items = dest;
        set->matches = (BobLauncherMatch**)(set->combined + n);
        atomic_fetch_add(&set->size, n - local_dups - INITIAL_UNFINISHED);
    } else if (local_dups) {
        atomic_fetch_sub(&set->size, local_dups);
    }
}

void hashset_prepare_new(HashSet* set) {
    uint64_t* hash_items = set->hash_items;
    int64_t n = set->hash_size;

    radix_sort_hash_asc(hash_items, (uint64_t*)set->combined, n);

    int dups = 0;
    for (int64_t i = 1; i < n; i++) {
        int64_t result = hash_items[i] - hash_items[i - 1];
        if (result < UINT32_MAX) {
            hash_items[i - 1] = 0;
            if (result < 0)
                hash_items[i] -= result;
            dups++;
        }
    }

    radix_sort_score(hash_items, set->combined + n, set->combined, n);

    set->score_items = set->combined;
    set->matches = (BobLauncherMatch**)(set->combined + n);
    atomic_store(&set->size, n - dups);
}

void container_return_sheet(HashSet* set, ResultContainer* container) {
    if (!container->current_sheet || container->current_sheet->size >= SHEET_SIZE) return;

    int pos = atomic_fetch_add(&set->write, 1);
    set->unfinished_queue[pos] = container->current_sheet;
    container->current_sheet = NULL;
}

void container_flush_items(HashSet* set, ResultContainer* container) {
    if (!set || !container->local_items_size) return;

    int write_start = atomic_fetch_add(&set->hash_size, container->local_items_size);

    memcpy(set->hash_items + write_start, container->local_items, container->local_items_size * sizeof(uint64_t));
    container->local_items_size = 0;
}

#define FOURTY_THREE_BITS 0x000007FFFFFFFFFFULL
#define FOURTY_EIGHT_BITS 0x0000FFFFFFFFFFFFULL

#define GET_FACTORY_USER_DATA(packed) \
    ((void*)(((packed) & FOURTY_THREE_BITS) << 4))

BobLauncherMatch* hashset_get_match_at(HashSet* set, int index) {
    if (atomic_load(&set->size) <= index) return NULL;

    uint32_t packed = set->score_items[index];
    if (packed != UINT32_MAX) {
        set->score_items[index] = UINT32_MAX;

        int sheet_idx = SHEET_IDX(packed);
        int item_idx = ITEM_IDX(packed);
        uint64_t match_data = set->sheet_pool[sheet_idx]->match_pool[item_idx];

        FuncPair pair = get_func_pair(match_data);
        void* user_data = GET_FACTORY_USER_DATA(match_data);
        set->matches[index] = ((MatchFactory)pair.match)(user_data);
    }

    return set->matches[index];
}

void hashset_destroy(HashSet* set) {
    if (!set) return;

    int old_capacity = atomic_exchange(&set->size, -1);
    int num_sheets = atomic_load(&set->global_index_counter);

    for (int i = 0; i < num_sheets; i++) {
        ResultSheet* sheet = set->sheet_pool[i];
        if (!sheet) continue;

        for (int64_t j = 0; j < sheet->size; j++) {
            uint64_t item = sheet->match_pool[j];
            FuncPair pair = get_func_pair(item);
            if (pair.destroy) {
                ((GDestroyNotify)pair.destroy)(GET_FACTORY_USER_DATA(item));
            }
        }
    }

    for (int i = 0; i < old_capacity; i++) {
        if (set->score_items[i] == UINT32_MAX) {
            g_object_unref(set->matches[i]);
        }
    }

    reset_hashset(set);
    if (return_hashset_to_pool(set)) {
        return;
    }

    for (int i = 0; i < num_sheets; i++) {
        free(set->sheet_pool[i]);
    }
    free(set->sheet_pool);
    free(set->combined);
    free(set->hash_items);
    free(set->counts);
    free(set);
}
