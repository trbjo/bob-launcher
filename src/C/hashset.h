#pragma once

#include <stddef.h>
#include "result-container.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

typedef struct _BobLauncherMatch BobLauncherMatch;
typedef BobLauncherMatch* (*MatchFactory)(void* user_data);

typedef void (*GDestroyNotify)(void* data);

typedef struct {
    // === CACHE LINE 1 ===
    atomic_int hash_size;           // 4B - frequent
    int event_id;                   // 4B - never
    uint64_t* hash_items;           // 8B - once
    BobLauncherMatch** matches;     // 8B - once
    uint32_t* score_items;          // 8B - once
    atomic_int size;                // 4B - once
    char _pad1[28];

    // === CACHE LINE 2 ===
    atomic_int write;               // 4B - frequent
    int _pad2;                      // 4B
    ResultSheet** sheet_pool;       // 8B - once
    char _pad3[48];

    // === CACHE LINE 3 ===
    _Atomic(ResultSheet**) read;    // 8B - frequent
    char _pad4[56];

    // === CACHE LINE 4 ===
    atomic_int global_index_counter;// 4B - medium
    char _pad5[60];

    // === Queue (each 8 pointers = own cache line) ===
    ResultSheet* unfinished_queue[MAX_SHEETS];
} HashSet;

HashSet* hashset_create(int event_id);
void hashset_destroy(HashSet* set);

void container_flush_items(HashSet* set, ResultContainer* container);
void container_return_sheet(HashSet* set, ResultContainer* container);
void hashset_merge_new(HashSet* set, ResultContainer* current);
void hashset_prepare(HashSet* hashset);
void hashset_prepare_new(HashSet* hashset);
ResultContainer* hashset_create_handle(HashSet* hashset, char* query, int16_t bonus, needle_info* string_info, needle_info* string_info_spaceless);
BobLauncherMatch* hashset_get_match_at(HashSet* set, int n);
ResultContainer* hashset_create_default_handle(HashSet* hashset, char* query);
