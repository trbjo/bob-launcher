#include "result-container.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdint.h>

extern int events_ok(unsigned int event_id);

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

// taken from the thread manager -- will probably work only on x86
static inline uint128_t pack_match_data(MatchFactory factory, void* factory_user_data,
                         GDestroyNotify factory_destroy, bool is_duplicate) {
    int64_t data_diff = (factory_user_data == NULL) ? 0 : (int64_t)((char*)factory_user_data - (char*)factory);
    int64_t destroy_diff = (factory_destroy == NULL) ? 0 : (int64_t)((char*)factory_destroy - (char*)factory);
    int64_t is_data_diff = llabs(data_diff) < llabs(destroy_diff);

    uint128_t packed = ((uint128_t)(int64_t)factory) >> PTR_ALIGN_SHIFT;
    packed |= (uint128_t)is_data_diff << PTR_BITS;
    packed |= (uint128_t)is_duplicate << (PTR_BITS + 1);
    packed |= (((uint128_t)(int64_t)(is_data_diff ? factory_destroy : factory_user_data)) >> SCND_PTR_ALIGN_SHIFT) << SECOND_PTR;
    packed |= (((uint128_t)(int64_t)(is_data_diff ? data_diff : destroy_diff)) >> PTR_ALIGN_SHIFT) << DIFF_START;

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
}


bool result_container_insert(ResultContainer* container, uint64_t hash, int16_t relevancy,
                          MatchFactory func, void* factory_user_data,
                          GDestroyNotify destroy_func, bool is_unique) {

    if (!container->current_sheet || container->current_sheet->size >= RESULTS_PER_SHEET) {
        if (!add_worker_sheet(container)) {
            return false;
        }
    }

    ResultSheet* sheet = container->current_sheet;

    uint64_t myhash = is_unique ? 0 : hash;
    uint64_t packed = PACK_MATCH(relevancy, sheet->global_index, sheet->size, myhash);
    container->items[container->size++] = packed;
    sheet->match_pool[sheet->size++] = pack_match_data(func, factory_user_data, destroy_func, false);
    return true;
}
