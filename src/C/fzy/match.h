#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "constants.h"

typedef int16_t score_t;

#define MATCH_MAX_LEN 512
#define INITIAL_CAPACITY 32

#define SWAP(x, y, T) do { T SWAP = x; x = y; y = SWAP; } while (0)
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

needle_info* prepare_needle(const char* needle);
void free_string_info(needle_info* info);

int query_has_match(const needle_info* needle, const char* haystack);
score_t match_score(const needle_info* needle, const char* haystack_str);

score_t match_positions(const needle_info* needle, const char* haystack, int* positions);
