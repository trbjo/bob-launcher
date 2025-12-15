#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "constants.h"

typedef int16_t score_t;

#define MATCH_MAX_LEN 1024
#define INITIAL_CAPACITY 128

#define MAX(a, b) (((a) > (b)) ? (a) : (b))

typedef struct {
    int len;
    int capacity;
    uint32_t* chars;
    uint32_t* unicode_upper;
} needle_info;

typedef struct {
    int len;
    score_t bonus[MATCH_MAX_LEN];
    uint32_t chars[MATCH_MAX_LEN];
} haystack_info;

typedef struct {
    uint16_t pos;
    uint16_t n_idx;
} haystack_index;

typedef struct {
    score_t D;
    score_t M;
} CacheCell;

needle_info* prepare_needle(const char* needle);
void free_string_info(needle_info* info);
int haystack_update(haystack_info* hay, const char* str, uint16_t common_chars, haystack_index* index, const needle_info* needle);
int query_has_match(const needle_info* needle, const char* haystack);
int setup_haystack_and_match(const needle_info* needle, haystack_info* hay, const char* haystack_str);
score_t match_positions(const needle_info* needle, const char* haystack, int* positions);
score_t match_score(const needle_info* needle, const char* haystack_str);
score_t match_score_with_haystack(const needle_info* needle, haystack_info* haystack);
score_t match_score_column_major(const needle_info* needle, const haystack_info* haystack, int start_col, CacheCell* cache);
