#include "hashset.h"
#include "thread-manager.h"
#include <stdlib.h>
#include <constants.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdint.h>

CACHE_ALIGNED __uint128_t g_func_pairs[MAX_FUNC_SLOTS];

static inline size_t GET_FUNC_IDX(uint64_t packed) {
    return (packed >> FUNC_PAIR_SHIFT) & 0xFF;
}

static inline FuncPair unpack_funcpair(size_t idx) {
    FuncPair pair;
    pair.packed = atomic128_load(&g_func_pairs[idx]);
    return pair;
}

static inline int find_or_add_pair(ResultContainer* rc, uintptr_t match, uintptr_t destroy) {
    FuncPair new_pair = { .match = match, .destroy = destroy };

    // Fast path: check cached slot
    if (atomic128_load(&g_func_pairs[rc->match_mre_idx]) == new_pair.packed)
        return rc->match_mre_idx;

    for (int i = 0; i < MAX_FUNC_SLOTS; i++) {
        __uint128_t current = atomic128_load(&g_func_pairs[i]);

        if (current == 0 && atomic128_cas(&g_func_pairs[i], &current, new_pair.packed)) {
            rc->match_mre_idx = i;
            return i;
        }

        if (current == new_pair.packed) {
            rc->match_mre_idx = i;
            return i;
        }
    }

    exit(127);
}

FuncPair get_func_pair(uint64_t packed) {
    size_t idx = GET_FUNC_IDX(packed);
    return unpack_funcpair(idx);
}

ALWAYS_INLINE const char* result_container_get_query(ResultContainer* self) {
    return self->query;
}

static inline bool grab_sheet_from_queue(ResultContainer* container) {
    ResultSheet** current_pos;
    __atomic_load(container->read, &current_pos, __ATOMIC_RELAXED);
    while (*current_pos) {
        ResultSheet** next_pos = current_pos + 1;
        if (atomic_compare_exchange_weak(container->read, &current_pos, next_pos)) {
            container->current_sheet = *current_pos;
            return true;
        }
        __builtin_ia32_pause();
    }
    return false;
}

static inline bool grab_fresh_sheet(ResultContainer* container) {
    int global_index = atomic_fetch_add(container->global_index_counter, 1);
    if (global_index >= MAX_SHEETS) {
        return false;
    }

    ResultSheet* sheet = malloc(sizeof(ResultSheet));
    if (!sheet) {
        return false;
    }

    sheet->size = 0;
    sheet->global_index = global_index;
    container->sheet_pool[global_index] = sheet;
    container->current_sheet = sheet;
    return true;
}

static uint64_t pack_match_data(ResultContainer* c,
                                MatchFactory f,
                                void* factory_user_data,
                                GDestroyNotify df) {
    int idx = find_or_add_pair(c, (uintptr_t)f, (uintptr_t)df);

    uint64_t packed = ((uint64_t)factory_user_data >> 4);
    packed |= ((uint64_t)idx) << FUNC_PAIR_SHIFT;

    return packed;
}

void container_destroy(ResultContainer* container) {
    container->local_items_size = 0;
    free(container->local_items);
    container->local_items = NULL;

    container->current_sheet = NULL;
    container->string_info = NULL;
    container->string_info_spaceless = NULL;
    container->query = NULL;

    free(container);
}

void container_flush_items(ResultContainer* container) {
    if (!container || !container->local_items_size) return;

    int write_start = atomic_fetch_add_explicit(container->global_items_size, container->local_items_size, memory_order_relaxed);

    memcpy(container->global_items + write_start, container->local_items, container->local_items_size * sizeof(uint64_t));
    container->local_items_size = 0;
}

static inline uint32_t hash_from_pointers(const void* p1, const void* p2, const void* p3, uint16_t high_bits) {
    const void* ptrs[] = { p1, p2, p3 };
    uint32_t hash = XXH32(ptrs, sizeof(ptrs), high_bits);  // Use as seed for better mixing
    return ((uint32_t)high_bits << 16) | (hash & 0xFFFF);
}

bool result_container_insert(ResultContainer* container, uint32_t hash, int32_t score,
                          MatchFactory func, void* factory_user_data,
                          GDestroyNotify destroy_func) {

    if (score <= SCORE_BELOW_THRESHOLD) {
        return false;
    }

    score -= SCORE_BELOW_THRESHOLD;
    score += container->bonus;
    score = ((score >= SCORE_MAX) ? SCORE_MAX - 1 :
                         (score < 0) ? 0 :
                         score);

    if (!container->current_sheet || container->current_sheet->size >= SHEET_SIZE) {
        if (!(grab_fresh_sheet(container) || grab_sheet_from_queue(container))) {
            return false;
        }
    }

    if (container->local_items_size >= SHEET_SIZE) {
        container_flush_items(container);
    }

    ResultSheet* sheet = container->current_sheet;

    if (!hash) {
        uint16_t index = (sheet->global_index << ITEM_BITS) | sheet->size;
        hash = hash_from_pointers(container, func, factory_user_data, index);
    }
    container->local_items[container->local_items_size++] = PACK_HASH(hash, score, sheet->global_index, sheet->size);
    sheet->match_pool[sheet->size++] = pack_match_data(container, func, factory_user_data, destroy_func);
    return true;
}
