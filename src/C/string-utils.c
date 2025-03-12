#include "string-utils.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>

int is_word_boundary(gunichar c) {
    if (c <= 127) {
        return !((65 <= c && c <= 90) || (97 <= c && c <= 122));  /* not A-Z or a-z */
    }
    return g_unichar_isspace(c);
}

int string_contains(const char* str, const char* substring) {
    return str && substring && strstr(str, substring) != NULL;
}

char* replace(const char *str, const char *chars, const char *replacement) {
    if (str == NULL || chars == NULL || replacement == NULL) {
        return NULL;
    }

    size_t str_len = strlen(str);
    size_t repl_len = strlen(replacement);

    if (str_len == 0) {
        return strdup("");
    }

    size_t max_size = str_len * repl_len + 1;  /* +1 for null terminator */

    if (repl_len <= 1) {
        max_size = str_len + 1;  /* +1 for null terminator */
    }

    /* Round up to 16 bytes for alignment */
    max_size = (max_size + 15) & ~15;

    /* Check for overflow */
    if (max_size < str_len) {
        return NULL;
    }

    char *result = malloc(max_size);
    if (!result) {
        return NULL;
    }

    size_t j = 0;
    for (size_t i = 0; i < str_len; i++) {
        if (strchr(chars, str[i])) {
            if (repl_len) {
                memcpy(&result[j], replacement, repl_len);
                j += repl_len;
            }
        } else {
            result[j++] = str[i];
        }
    }

    result[j] = '\0';

    /* Only realloc if we'll save significant space */
    if (j + 16 < max_size) {
        size_t final_size = (j + 16) & ~15;  /* Keep 16-byte alignment */
        char *final = realloc(result, final_size);
        return final ? final : result;
    }

    return result;
}


static char hex_to_char(char first, char second) {
    char hex[3] = {first, second, '\0'};
    return (char)strtol(hex, NULL, 16);
}

static int is_valid_percent_sequence(const char* str, size_t pos, size_t len) {
    return (pos + 2 < len) &&
           isxdigit((unsigned char)str[pos + 1]) &&
           isxdigit((unsigned char)str[pos + 2]);
}

char* decode_html_chars(const char* input) {
    if (input == NULL) {
        return NULL;
    }

    size_t input_len = strlen(input);
    char* output = (char*)malloc(input_len + 1);

    if (output == NULL) {
        return NULL;
    }

    size_t i = 0;  // input index
    size_t j = 0;  // output index

    while (i < input_len) {
        if (input[i] == '%') {
            if (is_valid_percent_sequence(input, i, input_len)) {
                // Valid %XX sequence
                output[j] = hex_to_char(input[i + 1], input[i + 2]);
                i += 3;
                j++;
            } else {
                // Invalid % sequence - preserve it
                output[j] = input[i];
                i++;
                j++;
            }
        } else if (input[i] == '+') {
            output[j] = ' ';
            i++;
            j++;
        } else {
            output[j] = input[i];
            i++;
            j++;
        }
    }

    output[j] = '\0';

    // Reallocate to exact size needed
    char* final = (char*)realloc(output, j + 1);
    return final ? final : output;
}

char* str_concat(const char* str1, const char* str2) {
    if (!str1) return strdup(str2);
    if (!str2) return strdup(str1);

    size_t len1 = strlen(str1);
    size_t len2 = strlen(str2);

    char* result = (char*)malloc(len1 + len2 + 1);
    if (!result) return NULL;

    memcpy(result, str1, len1);
    memcpy(result + len1, str2, len2 + 1); // +1 to include null terminator

    return result;
}

char* str_format(const char* format, ...) {
    va_list args;
    va_list args_copy;

    va_start(args, format);
    va_copy(args_copy, args);

    // Calculate required buffer size
    int size = vsnprintf(NULL, 0, format, args) + 1;
    va_end(args);

    char* buffer = (char*)malloc(size);
    if (buffer) {
        vsnprintf(buffer, size, format, args_copy);
    }

    va_end(args_copy);
    return buffer;
}

char** str_split(const char* str, const char* delimiter, int max_tokens) {
    if (!str) return NULL;

    // Count the number of delimiters to determine array size
    const char* tmp = str;
    int count = 1;
    while ((tmp = strstr(tmp, delimiter))) {
        count++;
        tmp += strlen(delimiter);
        if (max_tokens > 0 && count >= max_tokens) break;
    }

    // Allocate array
    char** parts = (char**)malloc((count + 1) * sizeof(char*));
    if (!parts) return NULL;

    // Split the string
    char* str_copy = strdup(str);
    if (!str_copy) {
        free(parts);
        return NULL;
    }

    int i = 0;
    char* token = strtok(str_copy, delimiter);
    while (token != NULL && (max_tokens <= 0 || i < max_tokens)) {
        parts[i++] = strdup(token);
        token = strtok(NULL, delimiter);
    }

    // If max_tokens reached but there's more string, add the rest
    if (max_tokens > 0 && i == max_tokens && token != NULL) {
        // Find the position in the original string
        const char* remaining = str;
        for (int j = 0; j < max_tokens - 1; j++) {
            remaining = strstr(remaining, delimiter) + strlen(delimiter);
        }
        parts[i++] = strdup(remaining);
    }

    parts[i] = NULL; // NULL-terminate the array
    free(str_copy);

    return parts;
}


int str_has_suffix(const char *str, const char *suffix) {
    if (!str || !suffix)
        return 0;

    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);

    if (suffix_len > str_len)
        return 0;

    return strcmp(str + str_len - suffix_len, suffix) == 0;
}
