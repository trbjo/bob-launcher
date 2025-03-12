#include "result-container.h"
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <math.h>

extern int events_ok(unsigned int event_id);


ALWAYS_INLINE const char* result_container_get_query(ResultContainer* self) {
    return self->query;
}

static bool add_worker_sheet(ResultContainer* container) {
    if (container->num_completed >= container->completed_capacity) {
        size_t new_sheet_capacity = container->completed_capacity == 0 ? 4 : container->completed_capacity * 2;
        ResultSheet** new_sheets = realloc(container->sheets, new_sheet_capacity * sizeof(ResultSheet*));
        if (!new_sheets) return false;
        container->sheets = new_sheets;
        container->completed_capacity = new_sheet_capacity;
    }

    size_t required_items_capacity = container->items_size;
    if (required_items_capacity >= container->items_capacity) {
        size_t new_items_capacity = container->items_capacity == 0 ? RESULTS_PER_SHEET : container->items_capacity * 2;
        while (new_items_capacity < required_items_capacity) {
            new_items_capacity *= 2;
        }
        MatchData** new_items = realloc(container->items, new_items_capacity * sizeof(MatchData*));
        if (!new_items) return false;
        container->items = new_items;
        container->items_capacity = new_items_capacity;
    }

    ResultSheet* new_sheet = malloc(sizeof(ResultSheet));
    if (!new_sheet) return false;

    new_sheet->match_pool = malloc(RESULTS_PER_SHEET * sizeof(MatchData));
    if (!new_sheet->match_pool) {
        free(new_sheet);
        return false;
    }

    new_sheet->size = 0;
    new_sheet->capacity = RESULTS_PER_SHEET;
    container->sheets[container->num_completed++] = new_sheet;

    return true;
}


static ResultSheet* get_current_sheet(ResultContainer* container) {
    return (container->num_completed > 0) ? (ResultSheet*)container->sheets[container->num_completed - 1] : NULL;
}

bool result_container_insert(ResultContainer* container, unsigned int hash, double relevancy,
                          MatchFactory func, void* factory_user_data,
                          GDestroyNotify destroy_func) {
    ResultSheet* current = get_current_sheet(container);

    if (!current || current->size >= current->capacity) {
        if (!add_worker_sheet(container)) {
            return false;
        }
        current = get_current_sheet(container);
    }

    MatchData* item = &current->match_pool[current->size];
    item->factory = func;
    item->factory_user_data = factory_user_data;
    item->factory_destroy = destroy_func;
    item->hash = hash;
    item->relevancy = relevancy;

    container->items[container->items_size++] = item;
    current->size++;
    return true;
}

static atomic_ushort next_hash = 0; // smaller than 65536

bool result_container_insert_unique(ResultContainer* container, double relevancy,
                          MatchFactory func, void* factory_user_data, GDestroyNotify destroy_func) {
    size_t hash = atomic_fetch_add(&next_hash, 1);
    return result_container_insert(container, hash, relevancy, func, factory_user_data, destroy_func);
}
