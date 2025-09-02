#include <stdatomic.h>
#include <unistd.h>
#include <glib-object.h>
#include <stdio.h>
#include <immintrin.h>
#include "string-utils.h"

#include <stdlib.h>
#include <string.h>
#include "hashset.h"

_Atomic int64_t lol = 0LL;

static int64_t get_monotonic_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + (int64_t)ts.tv_nsec / 1000;
}

static inline bool is_duplicate(ResultSheet** sheet_pool, uint64_t packed) {
    return sheet_pool[SHEET_IDX(packed)]->match_pool[ITEM_IDX(packed)] & DUPLICATE_FLAG;
}

static inline uint128_t get_match_item_value(ResultSheet** sheet_pool, uint64_t packed) {
    return sheet_pool[SHEET_IDX(packed)]->match_pool[ITEM_IDX(packed)];
}

static inline void mark_duplicate(ResultSheet** sheet_pool, uint64_t packed) {
    sheet_pool[SHEET_IDX(packed)]->match_pool[ITEM_IDX(packed)] |= DUPLICATE_FLAG;
}

HashSet* hashset_create(const char* query, int event_id) {
    HashSet* set = calloc(1, sizeof(HashSet));
    if (!set) return NULL;

    set->query = strdup(query);
    set->event_id = event_id;
    atomic_init(&set->size, -1); // -1 is unfinished, 0 is no matches found
    atomic_init(&set->global_index_counter, 0);
    atomic_init(&set->write, 0);
    atomic_init(&set->read, set->unfinished_queue);
    atomic_store(&lol, 0LL);

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
    container->sheet_pool = hashset->sheet_pool;
    container->global_index_counter = &hashset->global_index_counter;
    container->merges = 0;

    container->read = &hashset->read;

    return container;
}

static int compare(const void* a, const void* b) {
    uint64_t packed_a = *(uint64_t*)a;
    uint64_t packed_b = *(uint64_t*)b;

    if (packed_b > packed_a) return 1;
    if (packed_a > packed_b) return -1;
    return 0;
}

static inline void ensure_target_largest(ResultContainer** target, ResultContainer** source) {
    if ((*source)->size <= (*target)->size) return;

    ResultContainer* temp = *target;
    *target = *source;
    *source = temp;
}

static void merge_workers(ResultSheet** sheet_pool, ResultContainer* target, ResultContainer* source, int duplicates) {
    size_t total_size = target->size + source->size - duplicates;

    if (total_size > target->items_capacity) {
        // Since all matches end up in a single container, that container must end up
        // at ≈ global_index_counter * RESULTS_PER_SHEET matches. To reduce
        // repetitive reallocations, we try to set the final size instead of the needed
        // size. the 2 > is simply a heuristic that seems to work well in practice.
        // Non-final containers very rarely end up merging more than that.

        int new_capacity = target->merges > 2 ?
            atomic_load(target->global_index_counter) * RESULTS_PER_SHEET :
            total_size;

        uint64_t* new_items = realloc(target->items, new_capacity * sizeof(uint64_t));
        if (!new_items) return;
        target->items = new_items;
        target->items_capacity = new_capacity;
    }

    size_t result_index = total_size - 1;
    size_t target_index = target->size - 1;
    size_t source_index = source->size - 1;

    // Perform reverse merge to avoid extra memory allocation
    // we can do a lot of optimizations here given our domain knowledge:
    //
    // 1. Target will be equal to or larger than source
    // 2. The arrays are already sorted
    // 3. Duplicates will be lower relevance than their preferred counterparts
    // 4. All items marked as duplicates in this merge have their
    //    higher-relevance counterpart in either source or target.
    //    Since we process both arrays together, we're guaranteed to
    //    handle all duplicates before source is exhausted.
    //
    // So, we don't need to iterate through all of the elements of target.
    // Since source is smaller than or equal to target, all duplicates will have been
    // taken care of when we run out of source; because either it's in source itself,
    // or it's in target; but if it's in target, this means having the higher ranked
    // identity in source and that means that we won't run out of source until we've
    // inserted the higher ranking sibling from said source!
    // The two pairs of while loops is an optimization: if we don't have any duplicates
    // in the items being merged, we pick the loops that don't need to check.

    while (duplicates && source_index < source->size && target_index < target->size) {
        uint64_t t_packed = target->items[target_index];

        if (is_duplicate(sheet_pool, t_packed)) {
            target_index--;
            duplicates--;
            continue;
        }

        uint64_t s_packed = source->items[source_index];
        if (is_duplicate(sheet_pool, s_packed)) {
            source_index--;
            duplicates--;
            continue;
        }

        if (t_packed > s_packed) {
            target->items[result_index--] = s_packed;
            source_index--;
        } else {
            target->items[result_index--] = t_packed;
            target_index--;
        }
    }

    while (source_index < source->size && target_index < target->size) {
        uint64_t t_packed = target->items[target_index];
        uint64_t s_packed = source->items[source_index];
        if (t_packed > s_packed) {
            target->items[result_index--] = s_packed;
            source_index--;
        } else {
            target->items[result_index--] = t_packed;
            target_index--;
        }
    }

    while (duplicates && source_index < source->size) {
        uint64_t s_packed = source->items[source_index];
        if (!is_duplicate(sheet_pool, s_packed)) {
            target->items[result_index--] = s_packed;
        } else {
            duplicates--;
        }
        source_index--;
    }

    while (source_index < source->size) {
        target->items[result_index--] = source->items[source_index--];
    }

    target->size = total_size;
    target->merges++;
}

static int merge_and_detect_duplicates(ResultContainer* container,
                                               uint32_t* target_slot,
                                               uint32_t source_head) {
    int duplicates = 0;
    uint32_t* indirect = target_slot;
    uint32_t t_current = *target_slot;
    uint32_t s_current = source_head;

    while (t_current != UINT32_MAX && s_current != UINT32_MAX) {
        MatchNode* t_node = &container->all_nodes[t_current];
        MatchNode* s_node = &container->all_nodes[s_current];

        uint64_t t_multipack = t_node->multipack;
        uint64_t s_multipack = s_node->multipack;

        if (IDENTITY(t_multipack) == IDENTITY(s_multipack)) {
            if (RELEVANCY(t_multipack) > RELEVANCY(s_multipack)) {
                mark_duplicate(container->sheet_pool, s_multipack);
                *indirect = t_current;
                indirect = &t_node->next;
            } else {
                mark_duplicate(container->sheet_pool, t_multipack);
                *indirect = s_current;
                indirect = &s_node->next;
            }
            duplicates++;

            t_current = t_node->next;
            s_current = s_node->next;
        } else if (IDENTITY(t_multipack) < IDENTITY(s_multipack)) {
            *indirect = t_current;
            indirect = &t_node->next;
            t_current = t_node->next;
        } else {
            *indirect = s_current;
            indirect = &s_node->next;
            s_current = s_node->next;
        }
    }

    while (t_current != UINT32_MAX) {
        MatchNode* t_node = &container->all_nodes[t_current];
        *indirect = t_current;
        indirect = &t_node->next;
        t_current = t_node->next;
    }

    while (s_current != UINT32_MAX) {
        MatchNode* s_node = &container->all_nodes[s_current];
        *indirect = s_current;
        indirect = &s_node->next;
        s_current = s_node->next;
    }

    *indirect = UINT32_MAX;
    return duplicates;
}

static int bitmap_item_insert(ResultContainer* target, uint64_t packed, uint32_t node_index) {
    uint64_t item_id = IDENTITY(packed);
    if (!item_id) return 0; // item is unique

    uint32_t bit_pos = item_id & ((1ULL << LOG2_BITMAP_BITS) -1);
    target->bitmap[bit_pos >> 6] |= (1ULL << (bit_pos & 63));

    target->all_nodes[node_index].multipack = packed;
    target->all_nodes[node_index].next = UINT32_MAX;

    uint32_t* slot = &target->slots[bit_pos];
    uint32_t* indirect = slot;
    uint32_t current = *slot;

    while (current != UINT32_MAX) {
        MatchNode* current_node = &target->all_nodes[current];
        uint64_t candidate = current_node->multipack;

        if (IDENTITY(candidate) == item_id) {
            if (RELEVANCY(packed) > RELEVANCY(candidate)) {
                mark_duplicate(target->sheet_pool, candidate);
                target->all_nodes[node_index].next = current_node->next;
                *indirect = node_index;
            } else {
                mark_duplicate(target->sheet_pool, packed);
            }
            return 1;
        }

        if (IDENTITY(candidate) > item_id) break;
        indirect = &current_node->next;
        current = current_node->next;
    }

    target->all_nodes[node_index].next = current;
    *indirect = node_index;
    return 0;
}

static int insert_bitmap_items(ResultContainer* target, ResultContainer* source) {
    size_t needed = target->nodes_count + source->size;

    if (needed > target->nodes_capacity) {
        size_t new_capacity = needed * 3 / 2;
        if (new_capacity < 16) new_capacity = 16;

        MatchNode* new_nodes = realloc(target->all_nodes, new_capacity * sizeof(MatchNode));
        if (!new_nodes) return -1;

        target->all_nodes = new_nodes;
        target->nodes_capacity = new_capacity;
    }

    int duplicates = 0;
    size_t base_index = target->nodes_count;

    for (size_t i = 0; i < source->size; i++) {
        duplicates += bitmap_item_insert(target, source->items[i], base_index + i);
    }

    target->nodes_count += source->size;
    return duplicates;
}

static void steal_bitmap(ResultContainer* target, ResultContainer* source) {
    target->bitmap = source->bitmap;
    target->slots = source->slots;
    target->all_nodes = source->all_nodes;
    target->nodes_count = source->nodes_count;
    target->nodes_capacity = source->nodes_capacity;

    source->bitmap = NULL;
    source->slots = NULL;
    source->all_nodes = NULL;
    source->nodes_count = 0;
    source->nodes_capacity = 0;
}

static int bitmap_merge_with_collision_detection(ResultSheet** sheet_pool,
                                                ResultContainer* target,
                                                ResultContainer* source) {
    if (!source->size || !target->size) return 0;

    int duplicates = 0;

    if (!target->bitmap && !source->bitmap) {
        target->bitmap = calloc(BITMAP_SIZE, sizeof(uint64_t));
        target->slots = malloc((1ULL << LOG2_BITMAP_BITS) * sizeof(uint32_t));
        memset(target->slots, 0xFF, (1ULL << LOG2_BITMAP_BITS) * sizeof(uint32_t));

        size_t total_size = source->size + target->size;
        target->all_nodes = malloc(total_size * sizeof(MatchNode));
        target->nodes_capacity = total_size;
        target->nodes_count = 0;

        for (size_t i = 0; i < target->size; i++) {
            duplicates += bitmap_item_insert(target, target->items[i], target->nodes_count++);
        }

        for (size_t j = 0; j < source->size; j++) {
            duplicates += bitmap_item_insert(target, source->items[j], target->nodes_count++);
        }
        return duplicates;
    }

    if (source->bitmap && !target->bitmap) {
        duplicates = insert_bitmap_items(source, target);
        steal_bitmap(target, source);
        return duplicates;
    }

    if (!source->bitmap) {
        return insert_bitmap_items(target, source);
    }

    // Both have bitmaps - need to merge nodes arrays first
    size_t needed = target->nodes_count + source->nodes_count;
    if (needed > target->nodes_capacity) {
        size_t new_capacity = needed * 3 / 2;
        MatchNode* new_nodes = realloc(target->all_nodes, new_capacity * sizeof(MatchNode));
        if (!new_nodes) return -1;

        target->all_nodes = new_nodes;
        target->nodes_capacity = new_capacity;
    }

    size_t source_base = target->nodes_count;
    for (size_t i = 0; i < source->nodes_count; i++) {
        target->all_nodes[source_base + i] = source->all_nodes[i];
        if (source->all_nodes[i].next != UINT32_MAX) {
            target->all_nodes[source_base + i].next += source_base;
        }
    }
    target->nodes_count += source->nodes_count;

    for (int word_idx = 0; word_idx < BITMAP_SIZE; word_idx++) {
        uint64_t word = source->bitmap[word_idx];
        target->bitmap[word_idx] |= word;

        while (word) {
            size_t slot_idx = (word_idx << 6) + __builtin_ctzll(word);
            word &= word - 1;

            uint32_t source_slot = source->slots[slot_idx];
            if (source_slot != UINT32_MAX) {
                source_slot += source_base;
                duplicates += merge_and_detect_duplicates(target, &target->slots[slot_idx], source_slot);
            }
        }
    }

    free(source->all_nodes);
    source->all_nodes = NULL;
    source->nodes_count = 0;
    source->nodes_capacity = 0;

    return duplicates;
}

static inline void return_sheet(HashSet* set, ResultContainer* container) {
    if (!container->current_sheet) return;
    if (container->current_sheet->size == RESULTS_PER_SHEET) return;

    int pos = atomic_fetch_add(&set->write, 1);
    set->unfinished_queue[pos] = container->current_sheet;
}

void hashset_merge(HashSet* set, ResultContainer* current) {
    if (atomic_load(&lol) == 0LL) {
        int64_t placeholder = 0LL;
        atomic_compare_exchange_strong(&lol, &placeholder, get_monotonic_time());
    }

    return_sheet(set, current);
    if (!current->items) {
        container_destroy(current);
        return;
    }

    qsort(current->items, current->size, sizeof(uint64_t), compare);

    // We will loop here until we can't grab a container.
    // "Prepared" is a slot that all workers insert their finished container into
    // if it's not empty, that means a container has to merge with that. if it is empty
    // the worker inserts its own container and exits. Note that this works whether or
    // not the worker actually merged with any other containers.
    while (1) {
        ResultContainer* previous = atomic_exchange(&set->prepared, NULL);
        if (previous) {
            ensure_target_largest(&current, &previous);
            int duplicates = bitmap_merge_with_collision_detection(set->sheet_pool, current, previous);
            merge_workers(set->sheet_pool, current, previous, duplicates);
            container_destroy(previous);
        } else if (atomic_compare_exchange_weak(&set->prepared, &previous, current)) {
            return;
        }
    }
}

void hashset_prepare(HashSet* set) {
    if (!set->prepared) return;
    set->matches = calloc(set->prepared->size, sizeof(BobLauncherMatch*));
    atomic_store(&set->size, set->prepared->size);
    int64_t delta_us = get_monotonic_time() - atomic_load(&lol);
    double delta_ms = delta_us / 1000.0;
    printf("Time taken: %.2f ms, results: %d\n", delta_ms, set->size);
}

BobLauncherMatch* hashset_get_match_at(HashSet* set, int index) {
    if (index >= atomic_load(&set->size)) {
        return NULL;
    }

    if (set->matches[index] == NULL) {
        uint64_t packed = set->prepared->items[index];
        uint128_t match_data = get_match_item_value(set->sheet_pool, packed);
        MatchFactory factory = get_factory(match_data);
        void* user_data = get_factory_user_data(match_data);
        set->matches[index] = factory(user_data);
    }

    return set->matches[index];
}

void hashset_destroy(HashSet* set) {
    if (!set) return;

    int old_capacity = atomic_exchange(&set->size, 0);

    for (int i = 0; i < MAX_SHEETS; i++) {
        ResultSheet* sheet = set->sheet_pool[i];
        if (sheet) {
            for (int j = 0; j < sheet->size; j++) {
                uint128_t item = sheet->match_pool[j];
                GDestroyNotify destroy = get_factory_destroy(item);
                if (destroy) {
                    destroy(get_factory_user_data(item));
                }
            }
            free(sheet);
        }
    }

    container_destroy(set->prepared);

    for (int i = 0; i < old_capacity; i++) {
        if (set->matches[i]) {
            g_object_unref(set->matches[i]);
        }
    }
    free(set->matches);

    free(set->query);
    free_string_info(set->string_info);
    free_string_info(set->string_info_spaceless);
    free(set);
}
