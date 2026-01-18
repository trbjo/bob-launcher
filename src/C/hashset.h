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
#define MERGE_THREADS 7

typedef struct {
    atomic_int v;
    char _pad[PAD(sizeof(atomic_int))];
} padded_atomic_int;

typedef struct {
    // === CACHE LINE 1 ===
    atomic_int hash_size;
    int event_id;
    uint64_t* hash_items;
    uint32_t* combined;
    BobLauncherMatch** matches;
    uint32_t* score_items;
    atomic_int size;
    char _pad1[28];

    padded_atomic_int bar[2];
    uint16_t (*counts)[256];

    // === CACHE LINE 2 ===
    atomic_int write;
    int _pad2;
    ResultSheet** sheet_pool;
    char _pad3[48];

    // === CACHE LINE 3 ===
    _Atomic(ResultSheet**) read;
    char _pad4[56];

    // === CACHE LINE 4 ===
    atomic_int global_index_counter;
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
void hashset_prepare_new_parallel(HashSet* set);
int merge_hashset_parallel(HashSet* set, int tid);




ResultContainer* hashset_create_handle(HashSet* hashset, char* query, int16_t bonus, needle_info* string_info, needle_info* string_info_spaceless);
BobLauncherMatch* hashset_get_match_at(HashSet* set, int n);
ResultContainer* hashset_create_default_handle(HashSet* hashset, char* query);
