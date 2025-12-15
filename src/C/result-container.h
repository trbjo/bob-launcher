#pragma once

#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <limits.h>
#include "match.h"

typedef struct _BobLauncherMatch BobLauncherMatch;
typedef BobLauncherMatch* (*MatchFactory)(void* user_data);

#define CACHE_LINE_SIZE 64
#define ALWAYS_INLINE inline __attribute__((always_inline))
#define CACHE_ALIGNED __attribute__((aligned(CACHE_LINE_SIZE)))

#define LOG2_BITMAP_BITS 14
#define BITMAP_SIZE 256

#define FUNC_PAIR_SHIFT 43
#define MAX_FUNC_SLOTS 64

#define MAX_SHEETS 256          // 2^8
#define SHEET_SIZE 512   // 2^9
#define UNIQUE_SENTINEL 0
// max number of items = 256 * 512 = 131072

#define ITEM_BITS 9     // log2(512) = 9 bits for 512 items
#define SHEET_BITS 8    // log2(256) = 8 bits for 256 sheets

#define ITEM_SHIFT 0                                           // 0
#define SHEET_SHIFT (ITEM_BITS)                               // 9
#define SCORE_SHIFT (ITEM_BITS + SHEET_BITS)
#define HASH_SHIFT 32

#define PACK_HASH(hash, score, sheet_index, item_index) \
    ((uint64_t)(((uint64_t)item_index) << ITEM_SHIFT) | \
     (((uint64_t)sheet_index) << SHEET_SHIFT) | \
     (((uint64_t)score) << SCORE_SHIFT) | \
     (((uint64_t)hash) << HASH_SHIFT))

#define ITEM_IDX(packed) (((packed) >> ITEM_SHIFT) & ((1ULL << ITEM_BITS) - 1))
#define SHEET_IDX(packed) (((packed) >> SHEET_SHIFT) & ((1ULL << SHEET_BITS) - 1))


typedef union {
    struct {
        uintptr_t match;
        uintptr_t destroy;
    };
    __uint128_t packed;
} FuncPair;


typedef struct _BobLauncherMatch BobLauncherMatch;
typedef BobLauncherMatch* (*MatchFactory)(void* user_data);
typedef void (*GDestroyNotify)(void* data);

typedef struct ResultSheet {
    size_t size;
    int global_index;
    uint64_t match_pool[SHEET_SIZE];  // Inlined - 4KB
} ResultSheet;

typedef struct ResultContainer {
    uint64_t* local_items;
    ResultSheet* current_sheet;
    size_t local_items_size;
    int16_t bonus;
    int match_mre_idx;

    ResultSheet** sheet_pool;
    uint64_t* global_items;
    atomic_int* global_items_size;
    needle_info* string_info;
    needle_info* string_info_spaceless;
    uint64_t event_id;
    char* query;
    atomic_int* global_index_counter;
    _Atomic(ResultSheet**)* read;
} ResultContainer;

FuncPair get_func_pair(uint64_t packed);

void container_destroy(ResultContainer* container);

bool result_container_insert(ResultContainer* container, uint32_t hash, int32_t score,
                            MatchFactory func, void* factory_user_data,
                            GDestroyNotify destroy_func);

const char* result_container_get_query(ResultContainer* container);

#define result_container_has_match(container, haystack) query_has_match(((ResultContainer*)container)->string_info, haystack)
#define result_container_match_score(container, haystack) match_score(((ResultContainer*)container)->string_info, haystack)
#define result_container_match_score_spaceless(container, haystack) match_score(((ResultContainer*)container)->string_info_spaceless, haystack)

extern int events_ok(int event_id);
#define result_container_is_cancelled(container) (!events_ok(((ResultContainer*)(container))->event_id))

#define result_container_add_lazy_unique(container, score, factory, factory_user_data, destroy_notify) \
    result_container_insert(container, UNIQUE_SENTINEL, score, factory, factory_user_data, destroy_notify)

#define result_container_add_lazy(container, hash, score, func, factory_user_data, destroy_notify) \
    result_container_insert(container, hash, score, func, factory_user_data, destroy_notify)
