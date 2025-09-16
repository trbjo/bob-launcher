#include "result-container.h"
#include <stdlib.h>
#include <constants.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdint.h>

extern int events_ok(int event_id);

_Atomic uintptr_t g_match_funcs[MAX_FUNC_SLOTS];
_Atomic uintptr_t g_destroy_funcs[MAX_FUNC_SLOTS];

static inline int find_or_add_func(uintptr_t* global_array, int* mre_idx, uintptr_t func) {
    if (!func) return NULL_FUNC_IDX;
    if (global_array[*mre_idx] == func) {
        return *mre_idx;
    }

    uintptr_t* current_pos = global_array;

    while (func != *current_pos) {
        while (*current_pos != 0 && func != *current_pos) current_pos++;
        if (func == *current_pos) break;
        uintptr_t expected = 0;
        atomic_compare_exchange_weak((_Atomic uintptr_t*)current_pos, &expected, func);
    }

    int idx = current_pos - global_array;
    *mre_idx = idx;
    return idx;
}

ALWAYS_INLINE const char* result_container_get_query(ResultContainer* self) {
    return self->query;
}

static inline bool grab_sheet(ResultContainer* container) {
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

static inline void hashset_add_sheet(ResultContainer* container) {
    container->current_sheet = NULL;

    int global_index = atomic_fetch_add(container->global_index_counter, 1);
    if (global_index >= MAX_SHEETS) return;

    ResultSheet* sheet = malloc(sizeof(ResultSheet));
    if (!sheet) return;

    sheet->global_index = global_index;
    sheet->size = 0;

    container->sheet_pool[global_index] = sheet;
    container->current_sheet = sheet;
}

static inline bool add_worker_sheet(ResultContainer* container) {
    size_t required_items_capacity = container->size + RESULTS_PER_SHEET;
    if (required_items_capacity >= container->items_capacity) {
        uint64_t* items = realloc(container->items, required_items_capacity * sizeof(uint64_t));
        if (!items) return false;
        container->items = items;
        container->items_capacity = required_items_capacity;
    }

    if (!grab_sheet(container)) {
        hashset_add_sheet(container);
    }

    return container->current_sheet != NULL;
}

static uint64_t pack_match_data(ResultContainer* container, MatchFactory func,
                               void* factory_user_data, GDestroyNotify destroy_func) {
    int match_idx = find_or_add_func((uintptr_t*)g_match_funcs,
                                    &container->match_mre_idx, (uintptr_t)func);
    int destroy_idx = find_or_add_func((uintptr_t*)g_destroy_funcs,
                                      &container->destroy_mre_idx, (uintptr_t)destroy_func);

    uint64_t packed = ((uint64_t)factory_user_data >> 4);
    packed |= ((uint64_t)match_idx & 0x3F) << 51;
    packed |= ((uint64_t)destroy_idx & 0x3F) << 57;

    return packed;
}

void container_destroy(ResultContainer* container) {
    if (!container) return;

    free(container->items);
    container->items = NULL;
    free(container->bitmap);
    container->bitmap = NULL;

    free(container->slots);
    container->slots = NULL;

    free(container->all_nodes);

    container->current_sheet = NULL;
    container->size = 0;
    container->string_info = NULL;
    container->string_info_spaceless = NULL;
    container->query = NULL;

    free(container);
    container = NULL;
}

bool result_container_insert(ResultContainer* container, uint32_t hash, int16_t relevancy,
                          MatchFactory func, void* factory_user_data,
                          GDestroyNotify destroy_func) {

    if (relevancy <= SCORE_MIN) {
        return false;
    }
    // clamp to int16_t bounds if necessary
    int32_t adjusted_relevancy = (int32_t)container->bonus + (int32_t)relevancy;
    relevancy = (int16_t)((adjusted_relevancy > INT16_MAX) ? INT16_MAX :
                         (adjusted_relevancy < INT16_MIN) ? INT16_MIN :
                         adjusted_relevancy);


    if (!container->current_sheet || container->current_sheet->size >= RESULTS_PER_SHEET) {
        if (!add_worker_sheet(container)) {
            return false;
        }
    }

    ResultSheet* sheet = container->current_sheet;

    uint64_t packed = PACK_MATCH(relevancy, sheet->global_index, sheet->size, hash);
    container->items[container->size++] = packed;
    sheet->match_pool[sheet->size++] = pack_match_data(container, func, factory_user_data, destroy_func);
    return true;
}
