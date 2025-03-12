#ifndef RESULT_CONTAINER_H
#define RESULT_CONTAINER_H

#include <stdbool.h>
#include <stdatomic.h>
#include "match.h"

#define ALWAYS_INLINE inline __attribute__((always_inline))
#define RESULTS_PER_SHEET 128


typedef struct _BobLauncherMatch BobLauncherMatch;

typedef BobLauncherMatch* (*MatchFactory)(void* user_data);
typedef void (*GDestroyNotify)(void* data);

// 8-byte aligned match data with members ordered by size
typedef struct {
    double relevancy;           // 8 bytes
    MatchFactory factory;       // 8 bytes
    void* factory_user_data;    // 8 bytes
    GDestroyNotify factory_destroy; // 8 bytes
    uint32_t hash;             // 4 bytes
    uint32_t _padding;         // 4 bytes padding for alignment
} __attribute__((aligned(8))) MatchData;


// Result sheet aligned to 8 bytes
typedef struct {
    MatchData* match_pool;      // 8 bytes
    size_t capacity;           // 8 bytes
    size_t size;              // 8 bytes
} __attribute__((aligned(8))) ResultSheet;


typedef struct ResultContainer {
    ResultSheet** sheets;           // 8 bytes
    MatchData** items;              // 8 bytes
    size_t items_capacity;          // 8 bytes
    size_t items_size;              // 8 bytes
    size_t completed_capacity;      // 8 bytes
    size_t num_completed;           // 8 bytes
    int worker_index;               // 4 bytes

    // ResultContainer fields
    needle_info* string_info;           // 8 bytes
    needle_info* string_info_spaceless; // 8 bytes
    unsigned int event_id;              // 4 bytes
    char* query;                        // 8 bytes
} ResultContainer;

bool result_container_insert(ResultContainer* container, unsigned int hash, double relevancy,
                            MatchFactory func, void* factory_user_data,
                            GDestroyNotify destroy_func);


bool result_container_insert_unique(ResultContainer* container, double relevancy,
                         MatchFactory func, void* factory_user_data,
                         GDestroyNotify destroy_func);

const char* result_container_get_query(ResultContainer* container);

#define result_container_has_match(container, haystack) query_has_match(((ResultContainer*)container)->string_info, haystack)
#define result_container_match_score_with_offset(container, haystack, offset) match_score_with_offset(((ResultContainer*)container)->string_info, haystack, offset)
#define result_container_match_score(container, haystack) match_score(((ResultContainer*)container)->string_info, haystack)
#define result_container_match_score_spaceless(container, haystack) match_score(((ResultContainer*)container)->string_info_spaceless, haystack)

extern int events_ok(unsigned int event_id);
#define result_container_is_cancelled(container) (!events_ok(((ResultContainer*)(container))->event_id))

#define result_container_add_lazy_unique(container, relevancy, factory, factory_user_data, destroy_notify) \
    result_container_insert_unique(container, relevancy, factory, factory_user_data, destroy_notify)

#define result_container_add_lazy(container, hash, relevancy, func, factory_user_data, destroy_notify) \
    result_container_insert(container, (hash | (1U << 16)), relevancy, func, factory_user_data, destroy_notify)

#endif /* RESULT_CONTAINER_H */
