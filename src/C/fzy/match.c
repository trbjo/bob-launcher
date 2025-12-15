#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>

#include "match.h"
#include "bonus.h"
#include "config.h"

static inline uint32_t utf8_to_codepoint(const char* str, int* advance) {
    const uint8_t* s = (const uint8_t*)str;
    uint8_t first = s[0];

    // ASCII fast path - handles ~90% of typical text
    if (first < 0x80) {
        *advance = 1;
        return first;
    }

    // Lookup table for UTF-8 sequence lengths
    static const uint8_t utf8_len_table[256] = {
        // 0x00-0x7F: 1 byte (ASCII) - 128 elements
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        // 0x80-0xBF: continuation bytes (invalid as first byte) - 64 elements
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        // 0xC0-0xC1: invalid (overlong encoding) - 2 elements
        0,0,
        // 0xC2-0xDF: 2-byte sequences - 30 elements
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        // 0xE0-0xEF: 3-byte sequences - 16 elements
        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
        // 0xF0-0xF4: 4-byte sequences - 5 elements
        4,4,4,4,4,
        // 0xF5-0xFF: invalid - 11 elements
        0,0,0,0,0,0,0,0,0,0,0
    };

    int len = utf8_len_table[first];
    *advance = len;

    switch (len) {
        case 0: {
	        *advance = 1;
	        return 0xFFFD;
	    }
        case 2: {
            uint8_t second = s[1];
            if ((second & 0xC0) != 0x80) {
                *advance = 1;
                return 0xFFFD;
            }

            uint32_t codepoint = ((first & 0x1F) << 6) | (second & 0x3F);

            if (codepoint < 0x80) {
                *advance = 1;
                return 0xFFFD;
            }
            return codepoint;
        }

        case 3: {
            uint8_t second = s[1];
            uint8_t third = s[2];

            if (((second & 0xC0) != 0x80) || ((third & 0xC0) != 0x80)) {
                *advance = 1;
                return 0xFFFD;
            }

            uint32_t codepoint = ((first & 0x0F) << 12) | ((second & 0x3F) << 6) | (third & 0x3F);

            if (codepoint < 0x800 || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
                *advance = 1;
                return 0xFFFD;
            }
            return codepoint;
        }

        case 4: {
            uint8_t second = s[1];
            uint8_t third = s[2];
            uint8_t fourth = s[3];

            if (((second & 0xC0) != 0x80) || ((third & 0xC0) != 0x80) || ((fourth & 0xC0) != 0x80)) {
                *advance = 1;
                return 0xFFFD;
            }

            uint32_t codepoint = ((first & 0x07) << 18) | ((second & 0x3F) << 12) |
                       ((third & 0x3F) << 6) | (fourth & 0x3F);

            if (codepoint < 0x10000 || codepoint > 0x10FFFF) {
                *advance = 1;
                return 0xFFFD;
            }
            return codepoint;
        }

        default:
            *advance = 1;
            return 0xFFFD;
    }
}

int haystack_update(haystack_info* hay, const char* str, uint16_t common_chars,
                    haystack_index* index,
                    const needle_info* needle) {
    haystack_index start = index[common_chars];
    int pos = start.pos;
    int n_idx = start.n_idx;

    const unsigned char* s = (const unsigned char*)(str + common_chars);
    unsigned char last_ch = (pos > 0) ? (unsigned char)hay->chars[pos - 1] : '/';
    int byte_offset = common_chars;

    while (*s && pos < MATCH_MAX_LEN) {
        int char_len;
        uint32_t codepoint = utf8_to_codepoint((const char*)s, &char_len);

        hay->chars[pos] = codepoint;
        hay->bonus[pos] = COMPUTE_BONUS(last_ch, (unsigned char)codepoint);

        for (int b = 0; b < char_len; b++) {
            index[byte_offset + b] = (haystack_index){pos, n_idx};
        }

        if (n_idx < needle->len &&
            (codepoint == needle->chars[n_idx] || codepoint == needle->unicode_upper[n_idx])) {
            n_idx++;
        }

        last_ch = (unsigned char)codepoint;
        s += char_len;
        byte_offset += char_len;
        pos++;
    }

    index[byte_offset] = (haystack_index){pos, n_idx};
    hay->len = pos;

    return n_idx == needle->len;
}


int setup_haystack_and_match(const needle_info* needle, haystack_info* hay, const char* haystack_str) {
	if (!haystack_str || !*haystack_str) return 0;

	const uint32_t *n_chars = needle->chars;
	const uint32_t *n_upper = needle->unicode_upper;
	int pos = 0;

	while (*haystack_str && pos < MATCH_MAX_LEN) {
		int char_len;
		uint32_t curr = utf8_to_codepoint(haystack_str, &char_len);
		hay->chars[pos++] = curr;
		haystack_str += char_len;

		if (*n_chars && (curr == *n_chars || curr == *n_upper)) {
			n_chars++;
			n_upper++;
		}
	}

	hay->len = pos;
	return !*n_chars;
}


int query_has_match(const needle_info* needle, const char* haystack) {
	haystack_info hay_info;
	return setup_haystack_and_match(needle, &hay_info, haystack);
}

static inline void precompute_bonus(haystack_info *haystack) {
	unsigned char last_ch = '/';  // Starting value
	for (int i = 0; i < haystack->len; i++) {
		unsigned char current_ch = (unsigned char)haystack->chars[i];
		haystack->bonus[i] = COMPUTE_BONUS(last_ch, current_ch);
		last_ch = current_ch;
	}
}

static inline void match_row(const needle_info *needle,
								const haystack_info *haystack,
								const int row,
								score_t* curr_D,
								score_t* curr_M,
								const score_t* last_D,
								const score_t* last_M) {

	int n = needle->len;
	int m = haystack->len;
	int i = row;

	const uint32_t needle_char = needle->chars[row];
	const uint32_t needle_upper = needle->unicode_upper[row];

	score_t prev_score = SCORE_BELOW_THRESHOLD;
	score_t gap_score = i == n - 1 ? SCORE_GAP_TRAILING : SCORE_GAP_INNER;

	score_t prev_M, prev_D;

	for (int j = 0; j < m; j++) {
		if (haystack->chars[j] == needle_char || haystack->chars[j] == needle_upper) {
			score_t score = SCORE_BELOW_THRESHOLD;
			if (!i) {
				score = (j * SCORE_GAP_LEADING) + haystack->bonus[j];
			} else if (j) { /* i > 0 && j > 0*/
				score = MAX(
						prev_M + haystack->bonus[j],

						/* consecutive match, doesn't stack with match_bonus */
						prev_D + SCORE_MATCH_CONSECUTIVE);
			}
			prev_D = last_D[j];
			prev_M = last_M[j];
			curr_D[j] = score;
			curr_M[j] = prev_score = MAX(score, prev_score + gap_score);
		} else {
			prev_D = last_D[j];
			prev_M = last_M[j];
			curr_D[j] = SCORE_BELOW_THRESHOLD;
			curr_M[j] = prev_score = prev_score + gap_score;
		}
	}
}

score_t match_score(const needle_info* needle, const char* haystack_str) {
	if (*haystack_str == '\0') {
		return SCORE_BELOW_THRESHOLD;
	}

	if (needle->len == 0) {
		return SCORE_BELOW_THRESHOLD;
	}

	haystack_info haystack;
	if (!setup_haystack_and_match(needle, &haystack, haystack_str)) return SCORE_BELOW_THRESHOLD;
	precompute_bonus(&haystack);
	return match_score_with_haystack(needle, &haystack);
}

score_t match_score_with_haystack(const needle_info* needle, haystack_info* haystack) {
	const int n = needle->len;
	const int m = haystack->len;

	if (n > m) {
		/*
		 * Unreasonably large candidate: return no score
		 * If it is a valid match it will still be returned, it will
		 * just be ranked below any reasonably sized candidates
		 */
		return SCORE_BELOW_THRESHOLD;
	} else if (n == m) {
		/* Since this method can only be called with a haystack which
		 * matches needle. If the lengths of the strings are equal the
		 * strings themselves must also be equal (ignoring case).
		 */
		return SCORE_MAX;
	}

	/*
	 * D[][] Stores the best score for this position ending with a match.
	 * M[][] Stores the best possible score at this position.
	 */
	score_t D[haystack->len], M[haystack->len];

	for (int i = 0; i < n; i++) {
		match_row(needle, haystack, i, D, M, D, M);
	}

	return M[m - 1];
}

score_t match_score_column_major(
    const needle_info* needle,
    const haystack_info* haystack,
    int start_col,
    CacheCell* cache)
{
    const int n = needle->len;
    const int m = haystack->len;

    if (n > m) return SCORE_BELOW_THRESHOLD;
    if (n == m) return SCORE_MAX;

    // When n == 1, row 0 is also the last row
    const score_t gap_row0 = (n == 1) ? SCORE_GAP_TRAILING : SCORE_GAP_INNER;

    // Handle j == 0 separately (only if start_col == 0)
    if (start_col == 0) {
        uint32_t hay_char = haystack->chars[0];
        score_t bonus = haystack->bonus[0];

        for (int i = 0; i < n; i++) {
            if (hay_char == needle->chars[i] || hay_char == needle->unicode_upper[i]) {
                cache[i] = (CacheCell){bonus, bonus};
            } else {
                score_t gap = (i == n - 1) ? SCORE_GAP_TRAILING : SCORE_GAP_INNER;
                cache[i] = (CacheCell){SCORE_BELOW_THRESHOLD, SCORE_BELOW_THRESHOLD + gap};
            }
        }
        start_col = 1;
    }

    // Main loop
    for (int j = start_col; j < m; j++) {
        uint32_t hay_char = haystack->chars[j];
        score_t bonus = haystack->bonus[j];
        CacheCell* col = cache + j * n;
        CacheCell* prev = cache + (j - 1) * n;

        // i == 0: uses SCORE_GAP_LEADING formula, no diagonal
        {
            score_t left_M = prev[0].M;
            if (hay_char == needle->chars[0] || hay_char == needle->unicode_upper[0]) {
                score_t score = j * SCORE_GAP_LEADING + bonus;
                col[0] = (CacheCell){score, MAX(score, left_M + gap_row0)};
            } else {
                col[0] = (CacheCell){SCORE_BELOW_THRESHOLD, left_M + gap_row0};
            }
        }

        // i == 1 to n-1 (unified loop, no special-casing last row)
        for (int i = 1; i < n; i++) {
            CacheCell diag = prev[i - 1];
            score_t left_M = prev[i].M;
            score_t gap = (i == n - 1) ? SCORE_GAP_TRAILING : SCORE_GAP_INNER;

            if (hay_char == needle->chars[i] || hay_char == needle->unicode_upper[i]) {
                score_t score = MAX(diag.M + bonus, diag.D + SCORE_MATCH_CONSECUTIVE);
                col[i] = (CacheCell){score, MAX(score, left_M + gap)};
            } else {
                col[i] = (CacheCell){SCORE_BELOW_THRESHOLD, left_M + gap};
            }
        }
    }

    return cache[(m - 1) * n + (n - 1)].M;
}

score_t match_positions(const needle_info *needle, const char *haystack_str, int *positions) {
	if (!needle || needle->len == 0)
		return SCORE_MIN;

	haystack_info haystack;
	setup_haystack_and_match(needle, &haystack, haystack_str);
	precompute_bonus(&haystack);

	const int n = needle->len;
	const int m = haystack.len;

	if (m > MATCH_MAX_LEN || n > m) {
		/*
		 * Unreasonably large candidate: return no score
		 * If it is a valid match it will still be returned, it will
		 * just be ranked below any reasonably sized candidates
		 */
		return SCORE_MIN;
	}

	/*
	 * D[][] Stores the best score for this position ending with a match.
	 * M[][] Stores the best possible score at this position.
	 */
	score_t (*D)[MATCH_MAX_LEN], (*M)[MATCH_MAX_LEN];
	M = malloc(sizeof(score_t) * MATCH_MAX_LEN * n);
	D = malloc(sizeof(score_t) * MATCH_MAX_LEN * n);

	match_row(needle, &haystack, 0, D[0], M[0], D[0], M[0]);
	for (int i = 1; i < n; i++) {
		match_row(needle, &haystack, i, D[i], M[i], D[i - 1], M[i - 1]);
	}

	/* backtrace to find the positions of optimal matching */
	if (positions) {
		int match_required = 0;
		for (int i = n - 1, j = m - 1; i >= 0; i--) {
			for (; j >= 0; j--) {
				/*
				 * There may be multiple paths which result in
				 * the optimal weight.
				 *
				 * For simplicity, we will pick the first one
				 * we encounter, the latest in the candidate
				 * string.
				 */
				if (D[i][j] != SCORE_MIN &&
				    (match_required || D[i][j] == M[i][j])) {
					/* If this score was determined using
					 * SCORE_MATCH_CONSECUTIVE, the
					 * previous character MUST be a match
					 */
					match_required =
					    i && j &&
					    M[i][j] == D[i - 1][j - 1] + SCORE_MATCH_CONSECUTIVE;
					positions[i] = j--;
					break;
				}
			}
		}
	}

	score_t result = M[n - 1][m - 1];

	free(M);
	free(D);

	return result;

}

static void resize_if_needed(needle_info* info, int needed_size) {
	if (needed_size >= info->capacity) {
		int old_capacity = info->capacity;
		int new_capacity = info->capacity * 2;
		while (new_capacity <= needed_size) {
			new_capacity *= 2;
		}

		uint32_t* new_chars = realloc(info->chars, new_capacity * sizeof(uint32_t));
		uint32_t* new_unicode_upper = realloc(info->unicode_upper, new_capacity * sizeof(uint32_t));

		if (!new_chars || !new_unicode_upper) {
			if (new_chars && !new_unicode_upper) {
				free(new_chars);
			}
			if (new_unicode_upper && !new_chars) {
				free(new_unicode_upper);
			}

			info->chars = NULL;
			info->unicode_upper = NULL;
			return;
		}

		memset(new_chars + old_capacity, 0, (new_capacity - old_capacity) * sizeof(uint32_t));
		memset(new_unicode_upper + old_capacity, 0, (new_capacity - old_capacity) * sizeof(uint32_t));

		info->chars = new_chars;
		info->unicode_upper = new_unicode_upper;
		info->capacity = new_capacity;
	}
}

needle_info* prepare_needle(const char* needle) {
	if (!needle) {
		return NULL;
	}

	needle_info* info = calloc(1, sizeof(needle_info));
	if (!info) {
		return NULL;
	}

	// Initialize with default capacity
	info->capacity = INITIAL_CAPACITY;
	info->chars = calloc(info->capacity, sizeof(uint32_t));
	info->unicode_upper = calloc(info->capacity, sizeof(uint32_t));
	info->len = 0;

	if (!info->chars || !info->unicode_upper) {
		free_string_info(info);
		return NULL;
	}

	const char* s = needle;
	int pos = 0;

	while (*s) {
		// Ensure we have space for one more character
		resize_if_needed(info, pos + 1);
		if (!info->chars || !info->unicode_upper) {
			free_string_info(info);
			return NULL;
		}

		int char_len;
		uint32_t decoded_char = utf8_to_codepoint(s, &char_len);
		info->chars[pos] = decoded_char;

		// Generate uppercase variant
		if (decoded_char >= 'a' && decoded_char <= 'z') {
			info->unicode_upper[pos] = decoded_char - 32;
		} else if (decoded_char >= U'à' && decoded_char <= U'þ') {
			if (decoded_char == U'ß' || decoded_char == U'÷') {
				info->unicode_upper[pos] = decoded_char;
			} else {
				info->unicode_upper[pos] = decoded_char - 32;
			}
		} else if (decoded_char >= 0x0101 && decoded_char <= 0x017F && (decoded_char % 2 == 1)) {
			info->unicode_upper[pos] = decoded_char - 1; // Latin Extended-A lowercase to uppercase
		} else {
			info->unicode_upper[pos] = decoded_char;
		}

		s += char_len;
		pos++;
	}
	info->len = pos;
	return info;
}

void free_string_info(needle_info* info) {
	if (info) {
		free(info->chars);
		free(info->unicode_upper);
		free(info);
	}
}
