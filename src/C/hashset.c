#include <stdatomic.h>
#include <unistd.h>
#include <glib-object.h>
#include <stdio.h>
#include <immintrin.h>
#include "string-utils.h"

#include <stdlib.h>
#include <string.h>
#include "hashset.h"

// Bit layout for MatchNode (uint64_t):
// Bits 0-8:   item_index (9 bits)
// Bits 9-16:  sheet_index (8 bits)
// Bits 17-34: hash_high_18 (18 bits) - low 14 bits implicit from bitmap slot position
// Bits 35-46: relevancy (12 bits, shifted right by 3 for 8x coarser granularity)
// Bits 47-63: next (17 bits)

#define NODE_NEXT_NULL ((1U << NODE_NEXT_BITS) - 1)  // 131071
#define NODE_ITEM_INDEX_SHIFT 0
#define NODE_SHEET_INDEX_SHIFT 9
#define NODE_HASH_HIGH_SHIFT 17
#define NODE_RELEVANCY_SHIFT 35
#define NODE_NEXT_SHIFT 47

#define NODE_ITEM_INDEX_BITS 9
#define NODE_SHEET_INDEX_BITS 8
#define NODE_HASH_HIGH_BITS 18
#define NODE_RELEVANCY_BITS 12
#define NODE_NEXT_BITS 17

// Macros for opaque MatchNode access
#define GET_NODE_NEXT(node_ptr) \
    ((uint32_t)((*(node_ptr) >> NODE_NEXT_SHIFT) & ((1ULL << NODE_NEXT_BITS) - 1)))

#define SET_NODE_NEXT(node_ptr, value) \
    (*(node_ptr) = (*(node_ptr) & ~(((1ULL << NODE_NEXT_BITS) - 1) << NODE_NEXT_SHIFT)) | \
                   (((uint64_t)(value) & ((1ULL << NODE_NEXT_BITS) - 1)) << NODE_NEXT_SHIFT))

#define GET_NODE_MULTIPACK(node_ptr) (*(node_ptr))

#define SET_NODE_MULTIPACK(node_ptr, packed) \
    do { \
        uint64_t item_idx = ITEM_IDX(packed); \
        uint64_t sheet_idx = SHEET_IDX(packed); \
        uint64_t hash = IDENTITY(packed); \
        uint64_t hash_high = (hash) >> LOG2_BITMAP_BITS; \
        int16_t relevancy = RELEVANCY(packed); \
        uint64_t rel_packed = ((uint32_t)((relevancy) + 1024) >> 3) & ((1ULL << NODE_RELEVANCY_BITS) - 1); \
        *(node_ptr) = ((uint64_t)(item_idx) << NODE_ITEM_INDEX_SHIFT) | \
                      ((uint64_t)(sheet_idx) << NODE_SHEET_INDEX_SHIFT) | \
                      ((uint64_t)(hash_high) << NODE_HASH_HIGH_SHIFT) | \
                      ((uint64_t)(rel_packed) << NODE_RELEVANCY_SHIFT) | \
                      ((uint64_t)((1ULL << NODE_NEXT_BITS) - 1) << NODE_NEXT_SHIFT); \
    } while(0)

// Helper macros for bitmap node operations
#define GET_NODE_HASH_HIGH(node) \
    (((node) >> NODE_HASH_HIGH_SHIFT) & ((1ULL << NODE_HASH_HIGH_BITS) - 1))

#define GET_NODE_IDENTITY(node, slot_index) \
    ((GET_NODE_HASH_HIGH(node) << LOG2_BITMAP_BITS) | (slot_index))

#define GET_NODE_RELEVANCY(node) \
    ((int16_t)(((((node) >> NODE_RELEVANCY_SHIFT) & ((1ULL << NODE_RELEVANCY_BITS) - 1)) << 3) - 1024))

extern char* bob_launcher_match_get_title(BobLauncherMatch* match);

static inline uint64_t get_match_item_value(ResultSheet** sheet_pool, uint64_t packed) {
    return sheet_pool[SHEET_IDX(packed)]->match_pool[ITEM_IDX(packed)];
}

static inline bool is_duplicate(ResultSheet** sheet_pool, uint64_t packed) {
    size_t item_idx = ITEM_IDX(packed);
    return sheet_pool[SHEET_IDX(packed)]->duplicate_bits[item_idx >> 6] & (1ULL << (item_idx & 63));
}

static inline void mark_duplicate(ResultSheet** sheet_pool, uint64_t packed) {
    size_t item_idx = ITEM_IDX(packed);
    sheet_pool[SHEET_IDX(packed)]->duplicate_bits[item_idx >> 6] |= (1ULL << (item_idx & 63));
}

HashSet* hashset_create(int event_id) {
    HashSet* set = calloc(1, sizeof(HashSet));
    if (!set) return NULL;

    set->event_id = event_id;
    atomic_init(&set->size, -1); // -1 is unfinished, 0 is no matches found
    atomic_init(&set->global_index_counter, 0);
    atomic_init(&set->write, 0);
    atomic_init(&set->read, set->unfinished_queue);
    return set;
}

ResultContainer* hashset_create_handle(HashSet* hashset, const char* query, int16_t bonus) {
    ResultContainer* container = calloc(1, sizeof(ResultContainer));
    if (!container) return NULL;

    container->query = query;

    container->string_info = prepare_needle(query);
    char* query_spaceless = replace(query, " ", "");
    container->string_info_spaceless = prepare_needle(query_spaceless);
    free(query_spaceless);


    container->event_id = hashset->event_id;
    container->sheet_pool = hashset->sheet_pool;
    container->global_index_counter = &hashset->global_index_counter;
    container->merges = 0;
    container->bonus = bonus;

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

static int compare_strip_hash(const void* a, const void* b) {
    // by stripping the hash we force the insertion order to become the tiebreaker
    uint64_t mask = ~(0xFFFFFFFFULL << 17);
    uint64_t packed_a = *(uint64_t*)a & mask;
    uint64_t packed_b = *(uint64_t*)b & mask;

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
        // at ≈ global_index_counter * SHEET_SIZE matches. To reduce
        // repetitive reallocations, we try to set the final size instead of the needed
        // size. the 2 > is simply a heuristic that seems to work well in practice.
        // Non-final containers very rarely end up merging more than that.

        int new_capacity = target->merges > 2 ?
            atomic_load(target->global_index_counter) * SHEET_SIZE :
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
    uint32_t t_current = *target_slot;
    uint32_t s_current = source_head;
    uint32_t prev = NODE_NEXT_NULL;

    while (t_current < NODE_NEXT_NULL && s_current < NODE_NEXT_NULL) {
        MatchNode* t_node = &container->all_nodes[t_current];
        MatchNode* s_node = &container->all_nodes[s_current];

        uint64_t t_multipack = GET_NODE_MULTIPACK(t_node);
        uint64_t s_multipack = GET_NODE_MULTIPACK(s_node);

        uint32_t t_hash_high = GET_NODE_HASH_HIGH(t_multipack);
        uint32_t s_hash_high = GET_NODE_HASH_HIGH(s_multipack);

        if (t_hash_high == s_hash_high) {
            if (GET_NODE_RELEVANCY(t_multipack) > GET_NODE_RELEVANCY(s_multipack)) {
                mark_duplicate(container->sheet_pool, s_multipack);
                if (prev >= NODE_NEXT_NULL) {
                    *target_slot = t_current;
                } else {
                    SET_NODE_NEXT(&container->all_nodes[prev], t_current);
                }
                prev = t_current;
            } else {
                mark_duplicate(container->sheet_pool, t_multipack);
                if (prev >= NODE_NEXT_NULL) {
                    *target_slot = s_current;
                } else {
                    SET_NODE_NEXT(&container->all_nodes[prev], s_current);
                }
                prev = s_current;
            }
            duplicates++;

            t_current = GET_NODE_NEXT(t_node);
            s_current = GET_NODE_NEXT(s_node);
        } else if (t_hash_high < s_hash_high) {
            if (prev >= NODE_NEXT_NULL) {
                *target_slot = t_current;
            } else {
                SET_NODE_NEXT(&container->all_nodes[prev], t_current);
            }
            prev = t_current;
            t_current = GET_NODE_NEXT(t_node);
        } else {
            if (prev >= NODE_NEXT_NULL) {
                *target_slot = s_current;
            } else {
                SET_NODE_NEXT(&container->all_nodes[prev], s_current);
            }
            prev = s_current;
            s_current = GET_NODE_NEXT(s_node);
        }
    }

    while (t_current < NODE_NEXT_NULL) {
        MatchNode* t_node = &container->all_nodes[t_current];
        if (prev >= NODE_NEXT_NULL) {
            *target_slot = t_current;
        } else {
            SET_NODE_NEXT(&container->all_nodes[prev], t_current);
        }
        prev = t_current;
        t_current = GET_NODE_NEXT(t_node);
    }

    while (s_current < NODE_NEXT_NULL) {
        MatchNode* s_node = &container->all_nodes[s_current];
        if (prev >= NODE_NEXT_NULL) {
            *target_slot = s_current;
        } else {
            SET_NODE_NEXT(&container->all_nodes[prev], s_current);
        }
        prev = s_current;
        s_current = GET_NODE_NEXT(s_node);
    }

    if (prev >= NODE_NEXT_NULL) {
        *target_slot = NODE_NEXT_NULL;
    } else {
        SET_NODE_NEXT(&container->all_nodes[prev], NODE_NEXT_NULL);
    }

    return duplicates;
}

static int bitmap_item_insert(ResultContainer* target, uint64_t packed, uint32_t node_index) {
    uint64_t item_id = IDENTITY(packed);
    if (!item_id) return 0; // item is unique

    uint32_t bit_pos = item_id & ((1ULL << LOG2_BITMAP_BITS) -1);
    target->bitmap[bit_pos >> 6] |= (1ULL << (bit_pos & 63));

    MatchNode* new_node = &target->all_nodes[node_index];
    SET_NODE_MULTIPACK(new_node, packed);
    SET_NODE_NEXT(new_node, NODE_NEXT_NULL);

    uint32_t* slot = &target->slots[bit_pos];
    uint32_t current = *slot;
    uint32_t prev = NODE_NEXT_NULL;

    uint32_t item_id_high = item_id >> LOG2_BITMAP_BITS;

    while (current < NODE_NEXT_NULL) {
        MatchNode* current_node = &target->all_nodes[current];
        uint64_t candidate = GET_NODE_MULTIPACK(current_node);

        if (GET_NODE_HASH_HIGH(candidate) == item_id_high) {
            if (RELEVANCY(packed) > GET_NODE_RELEVANCY(candidate)) {
                mark_duplicate(target->sheet_pool, candidate);
                SET_NODE_NEXT(new_node, GET_NODE_NEXT(current_node));
                if (prev >= NODE_NEXT_NULL) {
                    *slot = node_index;
                } else {
                    SET_NODE_NEXT(&target->all_nodes[prev], node_index);
                }
            } else {
                mark_duplicate(target->sheet_pool, packed);
            }
            return 1;
        }

        if (GET_NODE_HASH_HIGH(candidate) > item_id_high) break;
        prev = current;
        current = GET_NODE_NEXT(current_node);
    }

    SET_NODE_NEXT(new_node, current);
    if (prev >= NODE_NEXT_NULL) {
        *slot = node_index;
    } else {
        SET_NODE_NEXT(&target->all_nodes[prev], node_index);
    }
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
        uint32_t next = GET_NODE_NEXT(&target->all_nodes[source_base + i]);
        if (next != NODE_NEXT_NULL) {
            SET_NODE_NEXT(&target->all_nodes[source_base + i], next + source_base);
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
            if (source_slot < NODE_NEXT_NULL) {
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
    if (container->current_sheet->size == SHEET_SIZE) return;

    int pos = atomic_fetch_add(&set->write, 1);
    set->unfinished_queue[pos] = container->current_sheet;
}

static ALWAYS_INLINE void _hashset_merge_common(HashSet* set, ResultContainer* current,
                                         int (*cmp)(const void*, const void*)) {
    if (!current->items) {
        container_destroy(current);
        return;
    }
    return_sheet(set, current);

    qsort(current->items, current->size, sizeof(uint64_t), cmp);

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

void hashset_merge_prefer_hash(HashSet* set, ResultContainer* current) {
    _hashset_merge_common(set, current, compare);
}

void hashset_merge_prefer_insertion(HashSet* set, ResultContainer* current) {
    _hashset_merge_common(set, current, compare_strip_hash);
}

void hashset_prepare(HashSet* set) {
    if (!set->prepared) return;
    set->matches = calloc(set->prepared->size, sizeof(BobLauncherMatch*));
    atomic_store(&set->size, set->prepared->size);
}

BobLauncherMatch* hashset_get_match_at(HashSet* set, int index) {
    while (1) {
        int size = atomic_load(&set->size);
        if (index >= size) return NULL;

        if (set->matches[index] != NULL) {
            return set->matches[index];
        }

        uint64_t packed = set->prepared->items[index];
        uint64_t match_data = get_match_item_value(set->sheet_pool, packed);
        MatchFactory factory = GET_MATCH_FACTORY(match_data);
        void* user_data = GET_FACTORY_USER_DATA(match_data);

        if (!user_data) {
            // item became invalid
            memmove(&set->prepared->items[index],
                   &set->prepared->items[index + 1],
                   (size - index - 1) * sizeof(uint64_t));

            atomic_fetch_sub(&set->size, 1);
        } else {
            set->matches[index] = factory(user_data);
            return set->matches[index];
        }
    }
}

void hashset_destroy(HashSet* set) {
    if (!set) return;

    int old_capacity = atomic_exchange(&set->size, 0);

    for (int i = 0; i < MAX_SHEETS; i++) {
        ResultSheet* sheet = set->sheet_pool[i];
        if (sheet) {
            for (int j = 0; j < sheet->size; j++) {
                uint64_t item = sheet->match_pool[j];
                GDestroyNotify destroy = GET_FACTORY_DESTROY(item);
                if (destroy) {
                    destroy(GET_FACTORY_USER_DATA(item));
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
    free(set);
}
