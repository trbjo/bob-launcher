#ifndef BOB_LAUNCHER_UTILS_H
#define BOB_LAUNCHER_UTILS_H

#include <glib.h>

int is_word_boundary(gunichar c);
char* replace(const char *str, const char *chars, const char *replacement);
char* str_concat(const char* str1, const char* str2);
char* str_format(const char* format, ...);
char** str_split(const char* str, const char* delimiter, int max_tokens);
int str_has_suffix(const char *str, const char *suffix);
int string_contains(const char* str, const char* substring);

/**
 * Decodes HTML/URL encoded characters in a string.
 *
 * This function converts:
 * - "%XX" sequences (where XX are hexadecimal digits) to their corresponding characters
 * - "+" characters to spaces
 * - Preserves invalid or incomplete percent sequences as-is
 *
 * @param input The input string containing HTML/URL encoded characters
 * @return A newly allocated string containing the decoded result, or NULL if:
 *         - input is NULL
 *         - memory allocation fails
 * @note The caller is responsible for freeing the returned string
 */
char* decode_html_chars(const char* input);


#endif
