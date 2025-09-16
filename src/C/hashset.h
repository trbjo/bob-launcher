#pragma once

#include <stddef.h>
#include "result-container.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#define CACHE_LINE_SIZE 64
typedef struct _BobLauncherMatch BobLauncherMatch;
typedef BobLauncherMatch* (*MatchFactory)(void* user_data);

#define ALWAYS_INLINE inline __attribute__((always_inline))


typedef void (*GDestroyNotify)(void* data);

typedef struct {
    // Group 1: Read-mostly data
    BobLauncherMatch** matches;
    ResultContainer* prepared;
    char* query;
    int event_id;
    needle_info* string_info;
    needle_info* string_info_spaceless;
    char _padding1[CACHE_LINE_SIZE - (5 * sizeof(void*) + sizeof(int))];

    // Group 2: Frequently updated size counter (own cache line)
    atomic_int size;
    char _padding2[CACHE_LINE_SIZE - sizeof(atomic_int)];

    // Group 3: Sheet storage arrays (naturally aligned, no contention)
    ResultSheet* sheet_pool[MAX_SHEETS];
    ResultSheet* unfinished_queue[MAX_SHEETS];

    // Group 4: Consumer hot spot (own cache line)
    ResultSheet** read;
    char _padding3[CACHE_LINE_SIZE - sizeof(void*)];

    // Group 5: Producer hot spot (own cache line)
    atomic_int write;
    char _padding4[CACHE_LINE_SIZE - sizeof(void*)];

    // Group 6: Global index counter (own cache line)
    atomic_int global_index_counter;
    char _padding5[CACHE_LINE_SIZE - sizeof(atomic_int)];
} __attribute__((aligned(CACHE_LINE_SIZE))) HashSet;

HashSet* hashset_create(const char* query, int event_id);
void hashset_destroy(HashSet* set);

void hashset_merge_prefer_hash(HashSet* set, ResultContainer* current);
#define hashset_merge hashset_merge_prefer_hash
void hashset_merge_prefer_insertion(HashSet* set, ResultContainer* current);

void hashset_prepare(HashSet* hashset);
ResultContainer* hashset_create_handle(HashSet* hashset, int16_t bonus);
BobLauncherMatch* hashset_get_match_at(HashSet* set, int n);

#define hashset_create_default_handle(set) hashset_create_handle(set, 0)
