#pragma once

#include <stddef.h>
#include "result-container.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

typedef struct _BobLauncherMatch BobLauncherMatch;
typedef BobLauncherMatch* (*MatchFactory)(void* user_data);

typedef void (*GDestroyNotify)(void* data);
typedef void (*TaskFunc)(void *data);

#define CACHELINE 64
#define PAD(sz) ((CACHELINE - ((sz) % CACHELINE)) % CACHELINE)

extern int hashset_merge_threads;
void hashset_init(int num_workers);

typedef struct {
    atomic_int v;
    char _pad[PAD(sizeof(atomic_int))];
} padded_atomic_int;

typedef struct {
    // === CACHE LINE 1 ===
    atomic_int hash_size;
    int event_id;
    int merge_workers;
    uint64_t* hash_items;
    uint32_t* combined;
    BobLauncherMatch** matches;
    uint32_t* score_items;
    atomic_int size;
    char _pad1[24];

    padded_atomic_int bar[2];
    uint32_t (*counts)[256];

    atomic_int write;
    int _pad2;
    ResultSheet** sheet_pool;
    _Atomic(ResultSheet**) read;
    atomic_int global_index_counter;
    ResultSheet* unfinished_queue[MAX_SHEETS];
} HashSet;

HashSet* hashset_create(int event_id);
void hashset_destroy(HashSet* set);

void container_return_sheet(HashSet* set, ResultContainer* container);
void hashset_merge_new(HashSet* set, ResultContainer* current);
void hashset_prepare(HashSet* hashset);
void hashset_prepare_new(HashSet* hashset);

int merge_hashset_parallel(HashSet* set, int tid);
#define hashset_prepare_new(set) merge_hashset_parallel(set, 0)

ResultContainer* hashset_create_handle(HashSet* hashset, const char* query, int16_t bonus, needle_info* string_info, needle_info* string_info_spaceless);
BobLauncherMatch* hashset_get_match_at(HashSet* set, int n);
ResultContainer* hashset_create_default_handle(HashSet* hashset, const char* query);
