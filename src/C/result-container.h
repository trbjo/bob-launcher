#ifndef RESULT_CONTAINER_H
#define RESULT_CONTAINER_H

#include <stdbool.h>
#include <stdatomic.h>
#include "match.h"

#define uint128_t __uint128_t

#define LOG2_BITMAP_BITS 14
#define BITMAP_SIZE 256
// 64 * 256 = 2^14

#define MAX_SHEETS 256          // 2^8
#define RESULTS_PER_SHEET 512   // 2^9

// Calculate masks automatically - much more human!
#define SHEET_MASK (MAX_SHEETS - 1)         // 256-1 = 0xFF (8 bits)
#define ITEM_MASK (RESULTS_PER_SHEET - 1)   // 512-1 = 0x1FF (9 bits)

// Calculate bit positions automatically
#define HASH_BITS 32
#define ITEM_BITS 9     // log2(512) = 9 bits for 512 items
#define SHEET_BITS 8    // log2(256) = 8 bits for 256 sheets
#define RELEVANCY_BITS 15

#define ITEM_SHIFT HASH_BITS                                    // 32
#define SHEET_SHIFT (HASH_BITS + ITEM_BITS)                     // 40
#define RELEVANCY_SHIFT (HASH_BITS + ITEM_BITS + SHEET_BITS)    // 49

// Updated bit layout comment:
// Bits 0-31:  hash/identity (32 bits)
// Bits 32-39: item index    (8 bits)  ← now 8 bits for 256 items
// Bits 40-48: sheet index   (9 bits)  ← now 9 bits for 512 sheets
// Bits 49-63: relevancy     (15 bits, signed, biased by 1024)

#define PACK_MATCH(relevancy, sheet_index, item_index, hash) \
    (((uint64_t)(hash) & 0xFFFFFFFF) | \
     ((uint64_t)((item_index) & ITEM_MASK) << ITEM_SHIFT) | \
     ((uint64_t)((sheet_index) & SHEET_MASK) << SHEET_SHIFT) | \
     ((uint64_t)((relevancy) + 1024) << RELEVANCY_SHIFT))

#define EXTRACT_ITEM_IDX(packed) (((packed) >> ITEM_SHIFT) & ITEM_MASK)
#define EXTRACT_SHEET_IDX(packed) (((packed) >> SHEET_SHIFT) & SHEET_MASK)
#define EXTRACT_RELEVANCY(packed) ((int16_t)(((packed) >> RELEVANCY_SHIFT) & 0x7FFF) - 1024)
#define EXTRACT_IDENTITY(packed) ((packed) & 0xFFFFFFFF) // hash part

#define PTR_ALIGN_SHIFT 4
#define PTR_BITS 43
#define SCND_PTR_BITS 45
#define SECOND_PTR 45
#define SCND_PTR_ALIGN_SHIFT 2
#define DIFF_START 90
#define DIFF_BITS 38
#define PTR_MASK ((1ULL << PTR_BITS) - 1)
#define SCND_PTR_MASK ((1ULL << SCND_PTR_BITS) - 1)
#define SIGN_EXTEND_SHIFT (64 - DIFF_BITS)
#define DUPLICATE_FLAG ((uint128_t)1 << ((PTR_BITS) + 1))
#define MATCH_FLAG_DUPLICATE (1ULL << 63)
#define DATA_FLAG ((uint128_t)1 << (PTR_BITS))

#define second_ptr(t) (int64_t)(((t >> SECOND_PTR) & SCND_PTR_MASK) << SCND_PTR_ALIGN_SHIFT)
#define third_ptr(t) \
    ({ \
        int64_t offset = ((int64_t)((t >> DIFF_START) << SIGN_EXTEND_SHIFT)) >> SIGN_EXTEND_SHIFT; \
        offset ? (int64_t)(((int64_t)(t & PTR_MASK) + offset) << PTR_ALIGN_SHIFT) : 0; \
    })

#define get_factory(packed) \
    (MatchFactory)(((int64_t)((packed) & PTR_MASK)) << PTR_ALIGN_SHIFT)

#define get_factory_user_data(packed) \
    (void*)(((packed) & DATA_FLAG) ? third_ptr(packed) : second_ptr(packed))

#define get_factory_destroy(packed) \
    (GDestroyNotify)(((packed) & DATA_FLAG) ? second_ptr(packed) : third_ptr(packed))

#define ALWAYS_INLINE inline __attribute__((always_inline))

typedef struct _BobLauncherMatch BobLauncherMatch;
typedef BobLauncherMatch* (*MatchFactory)(void* user_data);
typedef void (*GDestroyNotify)(void* data);

typedef struct MatchNode {
    uint64_t multipack;
    struct MatchNode* next;
} MatchNode;

typedef struct ResultSheet {
    uint128_t match_pool[RESULTS_PER_SHEET];
    size_t size;
    int global_index;
} __attribute__((aligned(8))) ResultSheet;

typedef struct ResultContainer {
    // Group 1: Thread-local working data
    ResultSheet* current_sheet;
    ResultSheet** sheet_pool;

    // Group 2: Queue pointers (these point to shared atomics)
    ResultSheet** unfinished_queue;
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
    MatchNode** slots;

    // Group 6: Memory management
    MatchNode** owned_blocks;
    size_t owned_count;
    size_t owned_capacity;

    // track merges for heuristic memory allocation
    int merges;
} ResultContainer;


void container_destroy(ResultContainer* container);

bool result_container_insert(ResultContainer* container, uint64_t hash, int16_t relevancy,
                            MatchFactory func, void* factory_user_data,
                            GDestroyNotify destroy_func, bool is_unique);

const char* result_container_get_query(ResultContainer* container);

#define result_container_has_match(container, haystack) query_has_match(((ResultContainer*)container)->string_info, haystack)
#define result_container_match_score_with_offset(container, haystack, offset) match_score_with_offset(((ResultContainer*)container)->string_info, haystack, offset)
#define result_container_match_score(container, haystack) match_score(((ResultContainer*)container)->string_info, haystack)
#define result_container_match_score_spaceless(container, haystack) match_score(((ResultContainer*)container)->string_info_spaceless, haystack)

extern int events_ok(unsigned int event_id);
#define result_container_is_cancelled(container) (!events_ok(((ResultContainer*)(container))->event_id))

#define result_container_add_lazy_unique(container, hash, relevancy, factory, factory_user_data, destroy_notify) \
    result_container_insert(container, ((uint64_t)hash), relevancy, factory, factory_user_data, destroy_notify, true)

#define result_container_add_lazy(container, hash, relevancy, func, factory_user_data, destroy_notify) \
    result_container_insert(container, ((uint64_t)hash), relevancy, func, factory_user_data, destroy_notify, false)

#endif /* RESULT_CONTAINER_H */
