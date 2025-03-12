#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>

#include "match.h"
#include "bonus.h"
#include "config.h"

static inline uint32_t utf8_to_codepoint(const char* str, int* advance) {
	uint8_t first = (uint8_t)str[0];
	if (first < 0x80) {
		*advance = 1;
		return first; // ASCII fast path
	}

	// 2-byte sequence: 110xxxxx 10xxxxxx
	if (first >= 0xC0 && first < 0xE0) {
		*advance = 2;
		return ((first & 0x1F) << 6) | (str[1] & 0x3F);
	}
	// 3-byte sequence: 1110xxxx 10xxxxxx 10xxxxxx
	else if (first < 0xF0) {
		*advance = 3;
		return ((first & 0x0F) << 12) | ((str[1] & 0x3F) << 6) | (str[2] & 0x3F);
	}
	// 4-byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
	else if (first < 0xF8) {
		*advance = 4;
		return ((first & 0x07) << 18) | ((str[1] & 0x3F) << 12) |
			   ((str[2] & 0x3F) << 6) | (str[3] & 0x3F);
	}

	*advance = 1;
	return 0; // Invalid UTF-8
}

static int setup_haystack_and_match(const needle_info* needle, haystack_info* hay, const char* haystack_str) {
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
	uint32_t last_ch = '/';  // Starting value
	const int n = haystack->len;
	for (int i = 0; i < n; i++) {
		uint32_t current_ch = haystack->chars[i];
		haystack->bonus[i] = COMPUTE_BONUS(last_ch, current_ch);
		last_ch = current_ch;
	}
}

score_t match_score_with_offset(const needle_info *needle, const char *haystack_str, unsigned int offset) {
	return match_score(needle, haystack_str + offset);
}

static inline void match_first_row(const needle_info *needle,
								   const haystack_info *haystack,
								   score_t *curr_D,
								   score_t *curr_M) {
	const int n = haystack->len;

	const uint32_t needle_char = needle->chars[0];  // First character of the needle
	const uint32_t needle_upper = needle->unicode_upper[0];

	score_t gap_score = 1 == needle->len ? SCORE_GAP_TRAILING : SCORE_GAP_INNER;
	score_t prev_score = SCORE_MIN;

	for (int j = 0; j < n; j++) {
		if (haystack->chars[j] == needle_char || haystack->chars[j] == needle_upper) {
			score_t score = (j * SCORE_GAP_LEADING) + haystack->bonus[j];
			curr_D[j] = score;
			curr_M[j] = prev_score = MAX(score, prev_score + gap_score);
		} else {
			curr_D[j] = SCORE_MIN;
			curr_M[j] = prev_score = prev_score + gap_score;
		}
	}
}


static inline void match_row(const needle_info *needle,
								const haystack_info *haystack,
								const int row,
								score_t *curr_D,
								score_t *curr_M,
								const score_t *last_D,
								const score_t *last_M) {
	const int n = needle->len;
	const int m = haystack->len;
	const uint32_t needle_char = needle->chars[row];
	const uint32_t needle_upper = needle->unicode_upper[row];
	const score_t gap_score = row == n - 1 ? SCORE_GAP_TRAILING : SCORE_GAP_INNER;
	score_t prev_score = SCORE_MIN;

	curr_D[0] = SCORE_MIN;
	curr_M[0] = prev_score = SCORE_MIN + gap_score;  // Reflects the first gap score

	for (int j = 1; j < m; j++) {
		score_t score = SCORE_MIN;
		if (haystack->chars[j] == needle_char || haystack->chars[j] == needle_upper) {
			score = MAX(
				last_M[j - 1] + haystack->bonus[j],
				last_D[j - 1] + SCORE_MATCH_CONSECUTIVE
			);
		}
		curr_D[j] = score;
		curr_M[j] = prev_score = MAX(score, prev_score + gap_score);
	}
}

score_t match_score(const needle_info* needle, const char* haystack_str) {
	if (needle == NULL || needle->len == 0 || haystack_str == NULL || *haystack_str == '\0') {
		return SCORE_MIN;
	}

	haystack_info haystack;
	if (!setup_haystack_and_match(needle, &haystack, haystack_str)) return SCORE_MIN;
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
	score_t D[2][MATCH_MAX_LEN], M[2][MATCH_MAX_LEN];

	score_t *last_D, *last_M;
	score_t *curr_D, *curr_M;

	last_D = D[0];
	last_M = M[0];
	curr_D = D[1];
	curr_M = M[1];

	// special case for i == 0
	match_first_row(needle, &haystack, last_D, last_M);

	for (int i = 1; i < n; i++) {
		match_row(needle, &haystack, i, curr_D, curr_M, last_D, last_M);

		SWAP(curr_D, last_D, score_t *);
		SWAP(curr_M, last_M, score_t *);
	}

	return last_M[m - 1];
}

score_t match_positions(const needle_info *needle, const char *haystack_str, int *positions) {
	if (!needle)
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
	score_t D[MATCH_MAX_LEN][MATCH_MAX_LEN];
	score_t M[MATCH_MAX_LEN][MATCH_MAX_LEN];
	score_t *last_D, *last_M;
	score_t *curr_D, *curr_M;

	last_D = &D[0][0];
	last_M = &M[0][0];

	match_first_row(needle, &haystack, last_D, last_M);

	for (int i = 1; i < n; i++) {
		curr_D = &D[i][0];
		curr_M = &M[i][0];

		match_row(needle, &haystack, i, curr_D, curr_M, last_D, last_M);

		last_D = curr_D;
		last_M = curr_M;
	}

	/* backtrace to find the positions of optimal matching */
	int match_required = 0;
	for (int i = n - 1, j = m - 1; i >= 0; i--) {
		for (; j >= 0; j--) {
			/*
			 * Convert j (character position) to byte offset
			 */
			if (D[i][j] > SCORE_MIN && (match_required || D[i][j] == M[i][j])) {
				match_required = i && j && M[i][j] == D[i - 1][j - 1] + SCORE_MATCH_CONSECUTIVE;
				positions[i] = j--;
				break;
			}
		}
	}

	return M[n - 1][m - 1];
}

static void resize_if_needed(needle_info* info, int needed_size) {
	if (needed_size >= info->capacity) {
		int new_capacity = info->capacity * 2;
		while (new_capacity <= needed_size) {
			new_capacity *= 2;
		}

		uint32_t* new_chars = realloc(info->chars, new_capacity * sizeof(uint32_t));
		uint32_t* new_unicode_upper = realloc(info->unicode_upper, new_capacity * sizeof(uint32_t));

		if (!new_chars || !new_unicode_upper) {
			// Handle allocation failure
			free(new_chars);  // safe to call with NULL
			free(new_unicode_upper);
			free(info->chars);
			free(info->unicode_upper);
			free(info);
			return;
		}

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
		} else if (decoded_char >= 0x00E0 && decoded_char <= 0x00FE && decoded_char != 0x00F7) {
			if (decoded_char == 0x00DF) {
				info->unicode_upper[pos] = decoded_char;  // Special case: ß remains unchanged
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
