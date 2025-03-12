#ifndef HASHSET_H
#define HASHSET_H

#include "match.h"
#include <stddef.h>
#include "result-container.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#define CACHE_LINE_SIZE 64

typedef struct _BobLauncherMatch BobLauncherMatch;
typedef BobLauncherMatch* (*MatchFactory)(void* user_data);

typedef void (*GDestroyNotify)(void* data);

typedef struct {
    BobLauncherMatch** matches;
    ResultContainer* first_worker;
    char* query;
    int event_id;
    char _padding1[CACHE_LINE_SIZE - (3 * sizeof(void*) + sizeof(int))];

    needle_info* string_info;
    needle_info* string_info_spaceless;
    atomic_int size;
    char _padding2[CACHE_LINE_SIZE - (2 * sizeof(void*) + sizeof(atomic_int))];

    atomic_int active_workers;
    char _padding3[CACHE_LINE_SIZE - sizeof(atomic_int)];
} __attribute__((aligned(CACHE_LINE_SIZE))) HashSet;

HashSet* hashset_create(const char* query, int event_id, int active_workers);
void hashset_destroy(HashSet* set);
int hashset_complete_merge(HashSet* hashset, ResultContainer* container);
ResultContainer* hashset_create_handle(HashSet* hashset);
BobLauncherMatch* hashset_get_match_at(HashSet* set, int n);

#endif // HASHSET_H
