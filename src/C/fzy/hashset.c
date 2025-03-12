#include <unistd.h>
#include <glib-object.h>
#include <immintrin.h>
#include "result-container.h"
#include "string-utils.h"
#include <stdio.h>
#include "match.h"

#include <stdlib.h>
#include <string.h>
#include "hashset.h"

HashSet* hashset_create(const char* query, int event_id, int active_workers) {
    HashSet* set = calloc(1, sizeof(HashSet));
    if (!set) return NULL;

    set->query = strdup(query);
    set->event_id = event_id;
    atomic_init(&set->size, -1); // -1 is unfinished, 0 is no matches found
    atomic_init(&set->active_workers, active_workers);
    set->first_worker = NULL; // Initialize the ready queue head

    set->string_info = prepare_needle(query);
    char* query_spaceless = replace(query, " ", "");
    set->string_info_spaceless = prepare_needle(query_spaceless);
    free(query_spaceless);

    return set;
}

ResultContainer* hashset_create_handle(HashSet* hashset) {
    ResultContainer* container = calloc(1, sizeof(ResultContainer));
    if (!container) return NULL;

    container->string_info = hashset->string_info;
    container->string_info_spaceless = hashset->string_info_spaceless;
    container->query = hashset->query;
    container->event_id = hashset->event_id;

    return container;
}

static int compare(const void* a, const void* b) {
    MatchData* ma = *(MatchData**)a;
    MatchData* mb = *(MatchData**)b;

    if (ma->hash == mb->hash) return 0;

    // Higher relevancy should come first
    if (mb->relevancy > ma->relevancy) return 1;
    if (ma->relevancy > mb->relevancy) return -1;

    // simply prefer lower hash values
    return (ma->hash > mb->hash) ? 1 : -1;
}

static void merge_workers(ResultContainer* target, ResultContainer* source) {
    // Ensure target always has the larger array
    if (source->items_capacity > target->items_capacity) {
        MatchData** temp_items = target->items;
        size_t temp_size = target->items_size;
        size_t temp_capacity = target->items_capacity;

        target->items = source->items;
        target->items_size = source->items_size;
        target->items_capacity = source->items_capacity;

        source->items = temp_items;
        source->items_size = temp_size;
        source->items_capacity = temp_capacity;
    }

    size_t total_size = target->items_size + source->items_size;

    if (total_size > target->items_capacity) {
        MatchData** new_items = realloc(target->items, total_size * sizeof(MatchData*));
        if (!new_items) return;
        target->items = new_items;
        target->items_capacity = total_size;
    }

    // Merge source into target
    size_t result_index = total_size - 1;
    size_t target_index = target->items_size - 1;
    size_t source_index = source->items_size - 1;

    // Perform reverse merge to avoid extra memory allocation
    while (target_index < target->items_size && source_index < source->items_size) {
        // Cast to MatchData* before comparison
        switch (compare(&target->items[target_index], &source->items[source_index])) {
            case 1:  // target > source
                target->items[result_index] = target->items[target_index];
                target_index--;
                break;
            case 0:  // elements are equal, just take one (target)
                target->items[result_index] = target->items[target_index];
                target_index--;
                source_index--;  // skip the duplicate in source
                total_size--;
                break;
            case -1: // target < source
                target->items[result_index] = source->items[source_index];
                source_index--;
                break;
        }
        result_index--;
    }

    while (source_index < source->items_size) {
        target->items[result_index--] = source->items[source_index--];
    }

    target->items_size = total_size;
    source->items_size = 0;
}

int hashset_complete_merge(HashSet* set, ResultContainer* container) {
    qsort(container->items, container->items_size, sizeof(MatchData*), compare);

    while (1) {
        ResultContainer* expected = atomic_load(&set->first_worker);
        if (atomic_compare_exchange_weak(&set->first_worker, &expected, !expected ? container : NULL)) {
            if (!expected) break;
            merge_workers(container, expected);
            free(expected);
        } else {
            _mm_pause();
        }
    }

    if (atomic_fetch_sub(&set->active_workers, 1) - 1 == 0) {
        set->matches = calloc(container->items_size, sizeof(BobLauncherMatch*));
        atomic_store(&set->size, container->items_size);
        return 1;
    }

    return 0;
}

BobLauncherMatch* hashset_get_match_at(HashSet* set, int index) {
    if (index >= atomic_load(&set->size)) {
        return NULL;
    }

    if (set->matches[index] == NULL) {
        MatchData* match_data = (MatchData*)set->first_worker->items[index];
        set->matches[index] = match_data->factory(match_data->factory_user_data);
    }

    return set->matches[index];
}

void hashset_destroy(HashSet* set) {
    if (!set) return;

    int old_capacity = atomic_exchange(&set->size, 0);

    // Destroy all factory data from sheets and free all ResultContainers
    ResultContainer* current = set->first_worker;
    if (current != NULL) {
        for (size_t j = 0; j < current->num_completed; j++) {
            ResultSheet* sheet = (ResultSheet*)current->sheets[j];
            for (size_t k = 0; k < sheet->size; k++) {
                MatchData* item = &sheet->match_pool[k];
                if (item && item->factory_destroy) {
                    item->factory_destroy(item->factory_user_data);
                }
            }
        }

        // Free container resources
        if (current->sheets) {
            for (size_t j = 0; j < current->num_completed; j++) {
                ResultSheet* sheet = (ResultSheet*)current->sheets[j];
                free(sheet->match_pool);
                free(sheet);
            }
            free(current->sheets);
        }
        free(current->items);
        free(current);
    }

    // Destroy all cached matches with simple array access
    if (set->matches) {
        for (int i = 0; i < old_capacity; i++) {
            if (set->matches[i]) {
                g_object_unref(set->matches[i]);
            }
        }
        free(set->matches);
    }

    free(set->query);
    free(set);
}
