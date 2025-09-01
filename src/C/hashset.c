#include <stdatomic.h>
#include <unistd.h>
#include <glib-object.h>
#include <stdio.h>
#include <immintrin.h>
#include "string-utils.h"

#include <stdlib.h>
#include <string.h>
#include "hashset.h"

static inline bool is_duplicate(ResultSheet** sheet_pool, uint64_t packed) {
    int sheet_idx = EXTRACT_SHEET_IDX(packed);
    int item_idx = EXTRACT_ITEM_IDX(packed);
    return sheet_pool[sheet_idx]->match_pool[item_idx] & DUPLICATE_FLAG;
}

static inline uint128_t get_match_item_value(ResultSheet** sheet_pool, uint64_t packed) {
    int sheet_idx = EXTRACT_SHEET_IDX(packed);
    int item_idx = EXTRACT_ITEM_IDX(packed);
    return sheet_pool[sheet_idx]->match_pool[item_idx];
}

static inline void mark_duplicate(ResultSheet** sheet_pool, uint64_t packed) {
    int sheet_idx = EXTRACT_SHEET_IDX(packed);
    int item_idx = EXTRACT_ITEM_IDX(packed);
    sheet_pool[sheet_idx]->match_pool[item_idx] |= DUPLICATE_FLAG;
}

#define get_is_duplicate_value(packed) \
    (bool)((packed) & DUPLICATE_FLAG)

HashSet* hashset_create(const char* query, int event_id) {
    HashSet* set = calloc(1, sizeof(HashSet));
    if (!set) return NULL;

    set->query = strdup(query);
    set->event_id = event_id;
    atomic_init(&set->size, -1); // -1 is unfinished, 0 is no matches found
    atomic_init(&set->global_index_counter, 0); // -1 is unfinished, 0 is no matches found
    atomic_init(&set->write, 0); // -1 is unfinished, 0 is no matches found
    atomic_init(&set->read, set->unfinished_queue); // -1 is unfinished, 0 is no matches found
    set->prepared = NULL;

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
    container->unfinished_queue = hashset->unfinished_queue;

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

static void merge_workers(ResultSheet** restrict sheet_pool, ResultContainer* restrict target, ResultContainer* restrict source, int duplicates) {
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

    uint64_t* restrict target_items = target->items;
    uint64_t* restrict source_items = source->items;

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
        uint64_t t_packed = target_items[target_index];

        if (is_duplicate(sheet_pool, t_packed)) {
            target_index--;
            duplicates--;
            continue;
        }

        uint64_t s_packed = source_items[source_index];
        if (is_duplicate(sheet_pool, s_packed)) {
            source_index--;
            duplicates--;
            continue;
        }

        if (t_packed > s_packed) {
            target_items[result_index--] = s_packed;
            source_index--;
        } else {
            target_items[result_index--] = t_packed;
            target_index--;
        }
    }

    while (source_index < source->size && target_index < target->size) {
        uint64_t t_packed = target_items[target_index];
        uint64_t s_packed = source_items[source_index];
        if (t_packed > s_packed) {
            target_items[result_index--] = s_packed;
            source_index--;
        } else {
            target_items[result_index--] = t_packed;
            target_index--;
        }
    }

    while (duplicates && source_index < source->size) {
        uint64_t s_packed = source_items[source_index];
        if (!is_duplicate(sheet_pool, s_packed)) {
            target_items[result_index--] = s_packed;
        } else {
            duplicates--;
        }
        source_index--;
    }

    while (source_index < source->size) {
        target_items[result_index--] = source_items[source_index--];
    }

    target->size = total_size;
    target->merges++;
}

static inline int merge_and_detect_duplicates(ResultSheet** restrict sheet_pool, MatchNode** restrict target_slot, MatchNode* restrict source_list) {
    int duplicates = 0;
    MatchNode** indirect = target_slot;
    MatchNode* t_current = *target_slot;
    MatchNode* s_current = source_list;

    while (t_current && s_current) {
        uint64_t t_id = EXTRACT_IDENTITY(t_current->multipack);
        uint64_t s_id = EXTRACT_IDENTITY(s_current->multipack);

        if (t_id == s_id) {
            int16_t t_relevancy = EXTRACT_RELEVANCY(t_current->multipack);
            int16_t s_relevancy = EXTRACT_RELEVANCY(s_current->multipack);

            if (t_relevancy > s_relevancy) {
                mark_duplicate(sheet_pool, s_current->multipack);
                *indirect = t_current;
                indirect = &t_current->next;
            } else {
                mark_duplicate(sheet_pool, t_current->multipack);
                *indirect = s_current;
                indirect = &s_current->next;
            }
            duplicates++;

            t_current = t_current->next;
            s_current = s_current->next;
        } else if (t_id < s_id) {
            *indirect = t_current;
            indirect = &t_current->next;
            t_current = t_current->next;
        } else {
            *indirect = s_current;
            indirect = &s_current->next;
            s_current = s_current->next;
        }
    }

    while (t_current) {
        *indirect = t_current;
        indirect = &t_current->next;
        t_current = t_current->next;
    }

    while (s_current) {
        *indirect = s_current;
        indirect = &s_current->next;
        s_current = s_current->next;
    }

    *indirect = NULL;
    return duplicates;
}

static int bitmap_item_insert(ResultSheet** restrict sheet_pool, uint64_t packed, uint64_t* restrict bitmap, MatchNode** restrict slots, MatchNode* restrict node) {
    uint64_t item_id = EXTRACT_IDENTITY(packed);
    if (!item_id) return 0; // item is unique

    uint32_t bit_pos = item_id & 0x3FFF;
    bitmap[bit_pos / 64] |= (1ULL << (bit_pos % 64));

    node->multipack = packed;
    MatchNode** indirect = &slots[bit_pos];

    while (*indirect) {
        uint64_t existing_id = EXTRACT_IDENTITY((*indirect)->multipack);
        if (existing_id == item_id) {
            int16_t new_relevancy = EXTRACT_RELEVANCY(packed);
            int16_t existing_relevancy = EXTRACT_RELEVANCY((*indirect)->multipack);

            if (new_relevancy > existing_relevancy) {
                mark_duplicate(sheet_pool, (*indirect)->multipack);
                node->next = (*indirect)->next;
                *indirect = node;
            } else {
                mark_duplicate(sheet_pool, packed);
            }
            return 1;
        }
        if (existing_id > item_id) break;
        indirect = &(*indirect)->next;
    }

    node->next = *indirect;
    *indirect = node;
    return 0;
}

static inline int insert_bitmap_items(ResultSheet** restrict sheet_pool, ResultContainer* restrict target, ResultContainer* restrict source) {
    if (target->owned_count + 1 > target->owned_capacity) {
        target->owned_capacity = (target->owned_count + 1) * 2;
        target->owned_blocks = realloc(target->owned_blocks,
                                       target->owned_capacity * sizeof(MatchNode*));
    }

    int duplicates = 0;

    uint64_t* restrict bitmap = target->bitmap;
    MatchNode** restrict slots = target->slots;
    uint64_t* restrict source_items = source->items;
    MatchNode* all_nodes = malloc(source->size * sizeof(MatchNode));

    target->owned_blocks[target->owned_count++] = all_nodes;

    for (size_t i = 0; i < source->size; i++) {
        duplicates += bitmap_item_insert(sheet_pool, source_items[i], bitmap, slots, all_nodes++);
    }
    return duplicates;
}

static inline void steal_bitmap(ResultContainer* target, ResultContainer* source) {
    target->bitmap = source->bitmap;
    target->slots = source->slots;
    target->owned_blocks = source->owned_blocks;
    target->owned_count = source->owned_count;
    target->owned_capacity = source->owned_capacity;

    source->bitmap = NULL;
    source->slots = NULL;
    source->owned_blocks = NULL;
    source->owned_count = 0;
    source->owned_capacity = 0;
}

static int bitmap_merge_with_collision_detection(ResultSheet** restrict sheet_pool, ResultContainer* restrict target, ResultContainer* restrict source) {
    if (!source->size || !target->size) return 0;

    int duplicates = 0;

    if (!target->bitmap && !source->bitmap) {
        target->bitmap = calloc(BITMAP_SIZE, sizeof(uint64_t));
        target->slots = calloc(1ULL << LOG2_BITMAP_BITS, sizeof(MatchNode*));
        target->owned_blocks = malloc(sizeof(MatchNode*));

        size_t total_size = source->size + target->size;
        target->owned_blocks[0] = malloc(total_size * sizeof(MatchNode));
        target->owned_capacity = 1;
        target->owned_count = 1;

        uint64_t* restrict bitmap = target->bitmap;
        MatchNode** restrict slots = target->slots;
        uint64_t* restrict target_items = target->items;
        uint64_t* restrict source_items = source->items;
        MatchNode* all_nodes = target->owned_blocks[0];

        for (size_t i = 0; i < target->size; i++) {
            duplicates += bitmap_item_insert(sheet_pool, target_items[i], bitmap, slots, all_nodes++);
        }

        for (size_t j = 0; j < source->size; j++) {
            duplicates += bitmap_item_insert(sheet_pool, source_items[j], bitmap, slots, all_nodes++);
        }
        return duplicates;
    }

    if (source->bitmap && !target->bitmap) {
        duplicates = insert_bitmap_items(sheet_pool, source, target);
        steal_bitmap(target, source);
        return duplicates;
    }

    if (!source->bitmap) {
        return insert_bitmap_items(sheet_pool, target, source);
    }

    uint64_t* restrict t_bitmap = target->bitmap;
    uint64_t* restrict s_bitmap = source->bitmap;
    MatchNode** restrict t_slots = target->slots;
    MatchNode** restrict s_slots = source->slots;

    for (int word_idx = 0; word_idx < BITMAP_SIZE; word_idx++) {
        t_bitmap[word_idx] |= s_bitmap[word_idx];

        uint64_t word = s_bitmap[word_idx];
        while (word) {
            int bit = __builtin_ctzll(word);
            size_t slot_idx = word_idx * 64 + bit;
            word &= word - 1;

            duplicates += merge_and_detect_duplicates(sheet_pool, &t_slots[slot_idx], s_slots[slot_idx]);
        }
    }

    if (target->owned_count + source->owned_count > target->owned_capacity) {
        target->owned_capacity = (target->owned_count + source->owned_count) * 2;
        target->owned_blocks = realloc(target->owned_blocks,
                                       target->owned_capacity * sizeof(MatchNode*));
    }

    for (size_t i = 0; i < source->owned_count; i++) {
        target->owned_blocks[target->owned_count++] = source->owned_blocks[i];
    }
    source->owned_blocks = NULL;
    source->owned_count = 0;
    source->owned_capacity = 0;

    return duplicates;
}

static inline void return_sheet(HashSet* set, ResultContainer* container) {
    if (!container->current_sheet) return;
    if (container->current_sheet->size == RESULTS_PER_SHEET) return;

    int pos = atomic_fetch_add(&set->write, 1);
    container->unfinished_queue[pos] = container->current_sheet;
}

void hashset_merge(HashSet* set, ResultContainer* current) {
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
