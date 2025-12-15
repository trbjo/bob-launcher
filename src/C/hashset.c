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
#define RADIX_THREADS 6

static HashSet* hashset_pool[MAX_POOLED_HASHSETS];
static _Atomic(HashSet**) pool_pos = hashset_pool;

static inline HashSet* grab_hashset_from_pool(void) {
    HashSet** current_pos;
    __atomic_load(&pool_pos, &current_pos, __ATOMIC_RELAXED);
    while (current_pos > hashset_pool) {
        HashSet** next_pos = current_pos - 1;
        if (atomic_compare_exchange_weak(&pool_pos, &current_pos, next_pos)) {
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
        if (atomic_compare_exchange_weak(&pool_pos, &current_pos, next_pos)) {
            *current_pos = set;
            return true;
        }
        __builtin_ia32_pause();
    }
    return false;
}

static void reset_hashset(HashSet* set) {
    if (set->matches) {
        free(set->matches);
        set->matches = NULL;
    }

    // hash_items and score_items share memory; restore from whichever is valid
    if (set->score_items) {
        set->hash_items = (uint64_t*)set->score_items;
        set->score_items = NULL;
    }

    int num_sheets = atomic_load(&set->global_index_counter);
    for (int i = 0; i < num_sheets; i++) {
        if (set->sheet_pool[i]) {  // NULL check for failed allocations
            set->sheet_pool[i]->size = 0;
        }
    }

    atomic_store(&set->size, -1);
    atomic_store(&set->hash_size, 0);
    atomic_store(&set->global_index_counter, 0);
    atomic_store(&set->write, 0);

    memset(set->unfinished_queue, 0, sizeof(set->unfinished_queue));
    atomic_store(&set->read, set->unfinished_queue);
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

    set->hash_items = malloc(MAX_SHEETS * SHEET_SIZE * sizeof(uint64_t));
    if (!set->hash_items) {
        free(set);
        return NULL;
    }

    set->sheet_pool = malloc(MAX_SHEETS * sizeof(ResultSheet*));
    if (!set->sheet_pool) {
        free(set->hash_items);
        free(set);
        return NULL;
    }

    set->matches = NULL;
    set->event_id = event_id;
    memset(set->unfinished_queue, 0, sizeof(set->unfinished_queue));

    atomic_init(&set->size, -1);
    atomic_init(&set->hash_size, 0);
    atomic_init(&set->global_index_counter, 0);
    atomic_init(&set->write, 0);
    atomic_init(&set->read, set->unfinished_queue);
    set->score_items = NULL;

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

    // Sort by byte 4 (bits 32-39)
    for (size_t i = 0; i < n; i++)
        count[ ((arr[i] >> 32) & 0xFF)]++;
    for (int i = 1; i < 256; i++)
        count[i] += count[i-1];
    for (size_t i = n; i-- > 0;)
        tmp[--count[ ((arr[i] >> 32) & 0xFF)]] = arr[i];

    memset(count, 0, sizeof(count));

    // Sort by byte 5 (bits 40-47)
    for (size_t i = 0; i < n; i++)
        count[ ((tmp[i] >> 40) & 0xFF)]++;
    for (int i = 1; i < 256; i++)
        count[i] += count[i-1];
    for (size_t i = n; i-- > 0;)
        arr[--count[ ((tmp[i] >> 40) & 0xFF)]] = tmp[i];

    memset(count, 0, sizeof(count));

    // Sort by byte 6 (bits 48-55)
    for (size_t i = 0; i < n; i++)
        count[ ((arr[i] >> 48) & 0xFF)]++;
    for (int i = 1; i < 256; i++)
        count[i] += count[i-1];
    for (size_t i = n; i-- > 0;)
        tmp[--count[ ((arr[i] >> 48) & 0xFF)]] = arr[i];

    memset(count, 0, sizeof(count));

    // Sort by byte 7 (bits 56-63)
    for (size_t i = 0; i < n; i++)
        count[((tmp[i] >> 56) & 0xFF)]++;
    for (int i = 1; i < 256; i++)
        count[i] += count[i-1];
    for (size_t i = n; i-- > 0;)
        arr[--count[((tmp[i] >> 56) & 0xFF)]] = tmp[i];
}

static void radix_sort_score(uint64_t* src, uint32_t* tmp, size_t n) {
    uint32_t* dest = (uint32_t*)src;
    if (n == 0) return;

    if (n == 1) {
        dest[0] = (uint32_t)src[0];
        return;
    }

    size_t count[256] = {0};

    // Pass 1: src (64-bit) -> tmp (32-bit), sort by bits 16-23
    for (size_t i = 0; i < n; i++)
        count[255 - ((src[i] >> 16) & 0xFE)]++;
    for (int i = 1; i < 256; i++)
        count[i] += count[i-1];
    for (size_t i = n; i-- > 0;)
        tmp[--count[255 - ((src[i] >> 16) & 0xFE)]] = (uint32_t)src[i];

    memset(count, 0, sizeof(count));

    // Pass 2: tmp -> dest (reusing src memory as uint32_t*), sort by bits 24-31
    for (size_t i = 0; i < n; i++)
        count[255 - ((tmp[i] >> 24) & 0xFF)]++;
    for (int i = 1; i < 256; i++)
        count[i] += count[i-1];
    for (size_t i = n; i-- > 0;)
        dest[--count[255 - ((tmp[i] >> 24) & 0xFF)]] = tmp[i];
}

#include <time.h>

// static struct timespec bench_start, bench_end;
// static double time_sort1_ms, time_dedup_ms, time_sort2_ms;

static inline double timespec_diff_ms(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000.0 +
           (end.tv_nsec - start.tv_nsec) / 1000000.0;
}


#define DEDUP 5
// #define DEDUP_END 11
#define SCORE_HIST 12
#define SCORE_SCATTER 13
#define SCORE_SORT 14


#define CACHELINE 64
#define PAD(sz) ((CACHELINE - ((sz) % CACHELINE)) % CACHELINE)

typedef struct {
    atomic_int v;
    char _pad[PAD(sizeof(atomic_int))];
} padded_atomic_int;

typedef struct {
    atomic_int count;
    atomic_int sense;
} sense_barrier_t;


typedef struct {
    uint64_t* arr;
    uint64_t* tmp;
    size_t n;

    int shifts[4];

    size_t local_counts[RADIX_THREADS][256] __attribute__((aligned(CACHELINE)));
    size_t write_offsets[RADIX_THREADS][256] __attribute__((aligned(CACHELINE)));
    // note no false sharing going on here, all array access is within cache lines!

    int local_dups[RADIX_THREADS];

    uint32_t* score_tmp;

    padded_atomic_int bar[2];


    HashSet* set;
} ParallelRadixState;


static inline bool barrier_guard(ParallelRadixState* s, int bi) {
    return atomic_fetch_add(&s->bar[bi].v, 1) == RADIX_THREADS - 1;
}

static inline void barrier_init(ParallelRadixState* s) {
    while (atomic_load(&s->bar[1].v) != 0)
        __builtin_ia32_pause();
}

static inline bool barrier_wait(ParallelRadixState* s, int* bi) {
    int idx = *bi;
    *bi ^= 1;

    if (atomic_fetch_add(&s->bar[idx].v, 1) == RADIX_THREADS - 1) {
        atomic_store(&s->bar[idx ^ 1].v, 0);  // reset next barrier
        return true;  // I'm last, others are waiting, do work then release
    } else {
        while (atomic_load(&s->bar[idx].v) > 0)
            __builtin_ia32_pause();
        return false;
    }
}

static inline void barrier_release(ParallelRadixState* s, int bi) {
    atomic_store(&s->bar[bi ^ 1].v, 0);
}

static void radix_pass_task(void* arg) {
    int* my_id_ptr = (int*)arg;
    int tid = *my_id_ptr;
    ParallelRadixState* s = (ParallelRadixState*)(my_id_ptr - tid) - 1;

    size_t n = s->n;
    size_t chunk = (n + RADIX_THREADS - 1) / RADIX_THREADS;
    size_t start = tid * chunk;
    size_t end = (tid + 1) * chunk;
    if (end > n) end = n;

    int bi = 0;

    barrier_init(s);

    // === HASH SORT (4 passes) ===
    for (int pass = 0; pass < 4; pass++) {
        int shift = s->shifts[pass];
        uint64_t* src = (pass & 1) ? s->tmp : s->arr;
        uint64_t* dst = (pass & 1) ? s->arr : s->tmp;

        memset(s->local_counts[tid], 0, sizeof(s->local_counts[0]));
        for (size_t i = start; i < end; i++)
            s->local_counts[tid][(src[i] >> shift) & 0xFF]++;

        if (barrier_wait(s, &bi)) {
            size_t running = 0;
            for (int b = 0; b < 256; b++) {
                for (int t = 0; t < RADIX_THREADS; t++) {
                    s->write_offsets[t][b] = running;
                    running += s->local_counts[t][b];
                }
            }
            barrier_release(s, bi);
        }

        size_t offsets[256];
        memcpy(offsets, s->write_offsets[tid], sizeof(offsets));
        for (size_t i = start; i < end; i++) {
            uint8_t byte = (src[i] >> shift) & 0xFF;
            dst[offsets[byte]++] = src[i];
        }

        if (barrier_wait(s, &bi)) {
            barrier_release(s, bi);
        }
    }

    // === DEDUP ===
    uint64_t* hash_items = s->arr;
    int local_dups = 0;
    for (size_t i = start + 1; i < end; i++) {
        int64_t result = hash_items[i] - hash_items[i - 1];
        if (result < UINT32_MAX) {
            hash_items[i - 1] = 0;
            if (result < 0)
                hash_items[i] -= result;
            local_dups++;
        }
    }

    if (barrier_wait(s, &bi)) {
        barrier_release(s, bi);
    }

    if (tid + 1 < RADIX_THREADS && end < s->n) {
        size_t dup_end = end;

        while (dup_end < s->n && hash_items[dup_end] == 0) {
            dup_end++;
        }

        if (dup_end < s->n) {
            uint64_t a = hash_items[end - 1];
            uint64_t b = hash_items[dup_end];
            int64_t result = hash_items[dup_end] - hash_items[end - 1];
            if (b - a < UINT32_MAX) {
                hash_items[end - 1] = 0;
                if (result < 0)
                    hash_items[dup_end] -= result;
                local_dups++;
            }
        }
    }

    s->local_dups[tid] = local_dups;

    // === SCORE SORT PASS 1 ===
    uint32_t* score_tmp = s->score_tmp;

    memset(s->local_counts[tid], 0, sizeof(s->local_counts[0]));
    for (size_t i = start; i < end; i++)
        s->local_counts[tid][255 - ((hash_items[i] >> 16) & 0xFE)]++;

    if (barrier_wait(s, &bi)) {
        size_t running = 0;
        for (int b = 0; b < 256; b++) {
            for (int t = 0; t < RADIX_THREADS; t++) {
                s->write_offsets[t][b] = running;
                running += s->local_counts[t][b];
            }
        }
        barrier_release(s, bi);
    }


    size_t offsets[256];
    memcpy(offsets, s->write_offsets[tid], sizeof(offsets));
    for (size_t i = start; i < end; i++) {
        uint8_t byte = 255 - ((hash_items[i] >> 16) & 0xFE);
        score_tmp[offsets[byte]++] = (uint32_t)hash_items[i];
    }

    if (barrier_wait(s, &bi)) {
        barrier_release(s, bi);
    }

    // === SCORE SORT PASS 2 ===
    uint32_t* dest = (uint32_t*)hash_items;

    memset(s->local_counts[tid], 0, sizeof(s->local_counts[0]));
    for (size_t i = start; i < end; i++)
        s->local_counts[tid][255 - ((score_tmp[i] >> 24) & 0xFF)]++;

    if (barrier_wait(s, &bi)) {
        size_t running = 0;
        for (int b = 0; b < 256; b++) {
            for (int t = 0; t < RADIX_THREADS; t++) {
                s->write_offsets[t][b] = running;
                running += s->local_counts[t][b];
            }
        }
        barrier_release(s, bi);
    }

    memcpy(offsets, s->write_offsets[tid], sizeof(offsets));
    for (size_t i = start; i < end; i++) {
        uint8_t byte = 255 - ((score_tmp[i] >> 24) & 0xFF);
        dest[offsets[byte]++] = score_tmp[i];
    }

    if (barrier_guard(s, bi)) {
        HashSet* set = s->set;
        set->score_items = dest;
        set->hash_items = NULL;
        set->matches = (BobLauncherMatch**)s->tmp;

        size_t total_dups = 0;
        for (int i = 0; i < RADIX_THREADS; i++)
            total_dups += s->local_dups[i];

        atomic_store(&set->size, n - total_dups);
        free(s->score_tmp);
        free(s);
    }
}

void hashset_prepare_new_parallel(HashSet* set) {
    size_t n = set->hash_size;
    if (n <= 1) {
        atomic_store(&set->size, n);
        return;
    }

    ParallelRadixState* s = aligned_alloc(64, sizeof(ParallelRadixState) + RADIX_THREADS * sizeof(int));
    if (!s) return;

    s->arr = set->hash_items;
    s->n = n;
    s->shifts[0] = 32; s->shifts[1] = 40; s->shifts[2] = 48; s->shifts[3] = 56;
    s->set = set;
    atomic_init(&s->bar[0].v, 0);
    atomic_init(&s->bar[1].v, -1); // block until we can malloc

    int* worker_ids = (int*)(s + 1);

    for (int i = 0; i < RADIX_THREADS; i++) {
        worker_ids[i] = i;
    }

    for (int i = 1; i < RADIX_THREADS; i++) {
        thread_pool_run((void(*)(void*))radix_pass_task, &worker_ids[i], NULL);
    }

    uint64_t* tmp = malloc(n * sizeof(uint64_t));
    if (!tmp) return;
    s->tmp = tmp;

    atomic_store(&s->bar[1].v, 0); // release

    uint32_t* score_tmp = malloc(n * sizeof(uint32_t));
    if (!score_tmp) { free(tmp); return; }
    s->score_tmp = score_tmp;

    radix_pass_task(&worker_ids[0]);
}

void hashset_prepare_new(HashSet* set) {
    uint64_t* hash_items = set->hash_items;
    size_t n = set->hash_size;

    uint64_t* tmp_one = malloc(n * sizeof(uint64_t));
    if (!tmp_one) return;

    radix_sort_hash_asc(hash_items, tmp_one, set->hash_size);

    int dups = 0;
    for (size_t i = 1; i < n; i++) {
        int64_t result = hash_items[i] - hash_items[i - 1];

        if (result < UINT32_MAX) {
            hash_items[i - 1] = 0;
            if (result < 0)
                hash_items[i] -= result;
            dups++;
        }
    }

    uint32_t* tmp_two = malloc(n * sizeof(uint32_t));
    if (!tmp_two) return;

    radix_sort_score(hash_items, tmp_two, n);

    set->score_items = (uint32_t*)hash_items;
    set->hash_items = NULL;

    set->matches = (BobLauncherMatch**)tmp_one;
    atomic_store(&set->size, n - dups);

    free(tmp_two);
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
    if (set->size <= index) return NULL;

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

        for (size_t j = 0; j < sheet->size; j++) {
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

    free(set->matches);
    set->matches = NULL;

    reset_hashset(set);
    if (return_hashset_to_pool(set)) {
        return;
    }

    for (int i = 0; i < num_sheets; i++) {
        free(set->sheet_pool[i]);
    }

    free(set->sheet_pool);

    if (set->score_items) {
        free(set->score_items);
    } else {
        free(set->hash_items);
    }

    free(set);
}
