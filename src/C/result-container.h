#pragma once

#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <limits.h>
#include "match.h"

typedef struct _BobLauncherMatch BobLauncherMatch;
typedef BobLauncherMatch* (*MatchFactory)(void* user_data);

#define LOG2_BITMAP_BITS 14
#define BITMAP_SIZE 256

#define MAX_SHEETS 256          // 2^8
#define RESULTS_PER_SHEET 512   // 2^9
// max number of sheets = 256 * 512 = 131072

#define HASH_BITS 32
#define ITEM_BITS 9     // log2(512) = 9 bits for 512 items
#define SHEET_BITS 8    // log2(256) = 8 bits for 256 sheets
#define RELEVANCY_BITS 15

#define ITEM_SHIFT 0                                           // 0
#define SHEET_SHIFT (ITEM_BITS)                               // 9
#define HASH_SHIFT (ITEM_BITS + SHEET_BITS)                   // 17
#define RELEVANCY_SHIFT (ITEM_BITS + SHEET_BITS + HASH_BITS)  // 49

#define MAX_FUNC_SLOTS 64
#define NULL_FUNC_IDX 0x3F  // 6 bits all set

// Bits 0-8:   item index    (9 bits) ← 512 items needs 9 bits
// Bits 9-16:  sheet index   (8 bits) ← 256 sheets needs 8 bits
// Bits 17-48: hash/identity (32 bits)
// Bits 49-63: relevancy     (15 bits, signed, biased by 1024)

#define PACK_MATCH(relevancy, sheet_index, item_index, hash) \
    (((uint64_t)(item_index) << ITEM_SHIFT) | \
     ((uint64_t)(sheet_index) << SHEET_SHIFT) | \
     ((uint64_t)(hash) << HASH_SHIFT) | \
     ((uint64_t)((relevancy) + 1024) << RELEVANCY_SHIFT))

#define ITEM_IDX(packed) (((packed) >> ITEM_SHIFT) & ((1ULL << ITEM_BITS) - 1))
#define SHEET_IDX(packed) (((packed) >> SHEET_SHIFT) & ((1ULL << SHEET_BITS) - 1))
#define IDENTITY(packed) (((packed) >> HASH_SHIFT) & 0xFFFFFFFF)
#define RELEVANCY(packed) ((int16_t)(((packed) >> RELEVANCY_SHIFT) & 0x7FFF) - 1024)

#define DUPLICATE_FLAG (1ULL << 63)
#define ALWAYS_INLINE inline __attribute__((always_inline))

extern _Atomic uintptr_t g_match_funcs[MAX_FUNC_SLOTS];
extern _Atomic uintptr_t g_destroy_funcs[MAX_FUNC_SLOTS];

#define GET_FACTORY_USER_DATA(packed) ((void*)(((packed) & 0x7FFFFFFFFFFFFULL) << 4))
#define GET_MATCH_FUNC_IDX(packed) ((int)(((packed) >> 51) & 0x3F))
#define GET_DESTROY_FUNC_IDX(packed) ((int)(((packed) >> 57) & 0x3F))

static inline MatchFactory GET_MATCH_FACTORY(uint64_t packed) {
    int idx = GET_MATCH_FUNC_IDX(packed);
    return (MatchFactory)atomic_load(&g_match_funcs[idx]);
}

static inline GDestroyNotify GET_FACTORY_DESTROY(uint64_t packed) {
    int idx = GET_DESTROY_FUNC_IDX(packed);
    return (idx == 0x3F) ? NULL : (GDestroyNotify)atomic_load(&g_destroy_funcs[idx]);
}

void print_function_debug_stats(void);


typedef struct _BobLauncherMatch BobLauncherMatch;
typedef BobLauncherMatch* (*MatchFactory)(void* user_data);
typedef void (*GDestroyNotify)(void* data);

typedef struct MatchNode {
    uint64_t multipack;
    uint32_t next;
} MatchNode;

typedef struct ResultSheet {
    uint64_t match_pool[RESULTS_PER_SHEET];
    size_t size;
    int global_index;
} __attribute__((aligned(8))) ResultSheet;

typedef struct ResultContainer {
    // Group 1: Thread-local working data
    ResultSheet* current_sheet;
    ResultSheet** sheet_pool;

    // Group 2: Queue pointer
    ResultSheet*** read;

    // Group 3: Item data
    uint64_t* items;
    size_t items_capacity;
    size_t size;

    // Group 4: Query-related data
    needle_info* string_info;
    needle_info* string_info_spaceless;
    uint64_t event_id;
    char* query;

    // Group 5: Shared pointers
    atomic_int* global_index_counter;
    uint64_t* bitmap;
    uint32_t* slots;

    // Group 6: Node storage
    MatchNode* all_nodes;
    size_t nodes_count;
    size_t nodes_capacity;

    // Track merges for heuristic memory allocation
    int merges;
    int16_t bonus;

    int match_mre_idx;
    int destroy_mre_idx;
} ResultContainer;

void container_destroy(ResultContainer* container);

bool result_container_insert(ResultContainer* container, uint32_t hash, int16_t relevancy,
                            MatchFactory func, void* factory_user_data,
                            GDestroyNotify destroy_func);

const char* result_container_get_query(ResultContainer* container);

#define result_container_has_match(container, haystack) query_has_match(((ResultContainer*)container)->string_info, haystack)
#define result_container_match_score_with_offset(container, haystack, offset) match_score(((ResultContainer*)container)->string_info, haystack + offset)
#define result_container_match_score(container, haystack) match_score(((ResultContainer*)container)->string_info, haystack)
#define result_container_match_score_spaceless(container, haystack) match_score(((ResultContainer*)container)->string_info_spaceless, haystack)

extern int events_ok(int event_id);
#define result_container_is_cancelled(container) (!events_ok(((ResultContainer*)(container))->event_id))

#define result_container_add_lazy_unique(container, relevancy, factory, factory_user_data, destroy_notify) \
    result_container_insert(container, 0, relevancy, factory, factory_user_data, destroy_notify)

#define result_container_add_lazy(container, hash, relevancy, func, factory_user_data, destroy_notify) \
    result_container_insert(container, hash, relevancy, func, factory_user_data, destroy_notify)

