#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "hashset.h"
#include "thread-manager.h"
#include "events.h"
#include "state.h"
#include "string-utils.h"
#include "bob-launcher.h"
#include "utf8-utils.h"


BobLauncherSearchingFor state_sf;
HashSet** state_providers = NULL;
int state_providers_length1 = 0;
static int _state_providers_size_ = 0;

int* state_selected_indices = NULL;
int state_selected_indices_length1 = 0;
static int _state_selected_indices_size_ = 0;

StringBuilder** state_queries = NULL;
int state_queries_length1 = 0;
static int _state_queries_size_ = 0;

int* state_cursor_positions = NULL;
int state_cursor_positions_length1 = 0;
static int _state_cursor_positions_size_ = 0;

/* Forward declarations */
int state_find_next_word(bool right);
extern void controller_start_search (const char* search_query);
void bob_launcher_main_container_update_layout(HashSet* provider, int selected_index);
extern void bob_launcher_query_container_adjust_label_for_query (const char* text, int cursor_position);
typedef void (*TaskFunc)(void *data);

/* StringBuilder functions */
StringBuilder* string_builder_new() {
    StringBuilder* sb = (StringBuilder*) malloc(sizeof(StringBuilder));
    if (!sb) return NULL;

    sb->capacity = 16;
    sb->str = (char*) malloc(sb->capacity);
    if (!sb->str) {
        free(sb);
        return NULL;
    }

    sb->str[0] = '\0';
    sb->len = 0;
    return sb;
}

void string_builder_free(StringBuilder* sb) {
    if (sb) {
        free(sb->str);
        free(sb);
    }
}

static inline const char* string_builder_get_str(StringBuilder* sb) {
    return sb ? sb->str : "";
}

bool string_builder_ensure_capacity(StringBuilder* sb, size_t new_len) {
    if (!sb) return false;

    if (new_len + 1 > sb->capacity) {
        size_t new_capacity = sb->capacity * 2;
        while (new_len + 1 > new_capacity) {
            new_capacity *= 2;
        }

        char* new_str = (char*) realloc(sb->str, new_capacity);
        if (!new_str) return false;

        sb->str = new_str;
        sb->capacity = new_capacity;
    }

    return true;
}

bool string_builder_insert(StringBuilder* sb, size_t pos, const char* text) {
    if (!sb || !text) return false;

    size_t text_len = strlen(text);
    if (text_len == 0) return true;

    if (pos > sb->len) pos = sb->len;

    size_t new_len = sb->len + text_len;
    if (!string_builder_ensure_capacity(sb, new_len)) {
        return false;
    }

    if (pos < sb->len) {
        memmove(sb->str + pos + text_len, sb->str + pos, sb->len - pos + 1);
    } else {
        sb->str[sb->len] = '\0';
    }

    memcpy(sb->str + pos, text, text_len);
    sb->len = new_len;
    sb->str[sb->len] = '\0';

    return true;
}

bool string_builder_erase(StringBuilder* sb, size_t pos, size_t len) {
    if (!sb || pos >= sb->len) return false;

    if (pos + len >= sb->len) {
        sb->str[pos] = '\0';
        sb->len = pos;
        return true;
    }

    memmove(sb->str + pos, sb->str + pos + len, sb->len - pos - len + 1);
    sb->len -= len;

    return true;
}

HashSet* state_empty_provider(int event_id) {
    return hashset_create(event_id);
}

void state_initialize() {
    state_sf = bob_launcher_SEARCHING_FOR_SOURCES;

    // Allocate arrays
    state_providers = (HashSet**) malloc(bob_launcher_SEARCHING_FOR_COUNT * sizeof(HashSet*));
    state_providers_length1 = bob_launcher_SEARCHING_FOR_COUNT;
    _state_providers_size_ = bob_launcher_SEARCHING_FOR_COUNT;

    state_queries = (StringBuilder**) malloc(bob_launcher_SEARCHING_FOR_COUNT * sizeof(StringBuilder*));
    state_queries_length1 = bob_launcher_SEARCHING_FOR_COUNT;
    _state_queries_size_ = bob_launcher_SEARCHING_FOR_COUNT;

    state_cursor_positions = (int*) malloc(bob_launcher_SEARCHING_FOR_COUNT * sizeof(int));
    state_cursor_positions_length1 = bob_launcher_SEARCHING_FOR_COUNT;
    _state_cursor_positions_size_ = bob_launcher_SEARCHING_FOR_COUNT;

    state_selected_indices = (int*) malloc(bob_launcher_SEARCHING_FOR_COUNT * sizeof(int));
    state_selected_indices_length1 = bob_launcher_SEARCHING_FOR_COUNT;
    _state_selected_indices_size_ = bob_launcher_SEARCHING_FOR_COUNT;

    for (int i = 0; i < bob_launcher_SEARCHING_FOR_COUNT; i++) {
        state_selected_indices[i] = 0;
        state_cursor_positions[i] = 0;
        state_providers[i] = state_empty_provider(0);
        state_queries[i] = string_builder_new();
    }
}

const char* state_get_query() {
    return string_builder_get_str(state_queries[state_sf]);
}

bool state_is_in_initial_state() {
    return state_sf == bob_launcher_SEARCHING_FOR_SOURCES &&
           hashset_get_match_at(state_providers[bob_launcher_SEARCHING_FOR_PLUGINS],
                                state_selected_indices[bob_launcher_SEARCHING_FOR_PLUGINS]) == NULL &&
           strcmp(state_get_query(), "") == 0;
}

HashSet* state_current_provider() {
    return state_providers[state_sf];
}

BobLauncherMatch* state_selected_plugin() {
    return hashset_get_match_at(state_providers[bob_launcher_SEARCHING_FOR_PLUGINS],
                               state_selected_indices[bob_launcher_SEARCHING_FOR_PLUGINS]);
}

BobLauncherMatch* state_selected_source() {
    return hashset_get_match_at(state_providers[bob_launcher_SEARCHING_FOR_SOURCES],
                               state_selected_indices[bob_launcher_SEARCHING_FOR_SOURCES]);
}

BobLauncherMatch* state_selected_action() {
    return hashset_get_match_at(state_providers[bob_launcher_SEARCHING_FOR_ACTIONS],
                               state_selected_indices[bob_launcher_SEARCHING_FOR_ACTIONS]);
}

BobLauncherMatch* state_selected_target() {
    return hashset_get_match_at(state_providers[bob_launcher_SEARCHING_FOR_TARGETS],
                               state_selected_indices[bob_launcher_SEARCHING_FOR_TARGETS]);
}

int state_update_provider(BobLauncherSearchingFor what, HashSet* new_provider, int selected_index) {
    if (new_provider->event_id < state_providers[state_sf]->event_id) {
        thread_pool_run((TaskFunc)hashset_destroy, new_provider, NULL);
        return 0;
    }

    state_selected_indices[what] = selected_index < 0 ? 0 :
                                              (selected_index >= new_provider->size ?
                                               new_provider->size - 1 : selected_index);

    if (new_provider->size <= 0) {
        state_selected_indices[what] = 0;
    }

    HashSet* old = state_providers[what];
    state_providers[what] = new_provider;
    thread_pool_run((TaskFunc)hashset_destroy, old, NULL);
    return 1;
}

void state_update_layout(BobLauncherSearchingFor what) {
    // Using the current sf value if CURRENT_CATEGORY is passed
    // This allows us to simulate a default parameter in C
    if (what != CURRENT_CATEGORY) {
        state_sf = what;
    }

    bob_launcher_main_container_update_layout(
        state_providers[state_sf],
        state_selected_indices[state_sf]
    );
}

void state_reset() {
    int new_event_id = events_increment();

    for (int i = bob_launcher_SEARCHING_FOR_PLUGINS; i < bob_launcher_SEARCHING_FOR_COUNT; i++) {
        state_update_provider(i, state_empty_provider(new_event_id), 0);

        // Free and recreate the string builders
        string_builder_free(state_queries[i]);
        state_queries[i] = string_builder_new();

        state_selected_indices[i] = 0;
        state_cursor_positions[i] = 0;
    }

    state_update_layout(bob_launcher_SEARCHING_FOR_SOURCES);
    bob_launcher_query_container_adjust_label_for_query("", 0);
}

void state_delete_line() {
    if (strcmp(state_get_query(), "") != 0) {
        string_builder_free(state_queries[state_sf]);
        state_queries[state_sf] = string_builder_new();

        controller_start_search("");
        state_cursor_positions[state_sf] = 0;
        bob_launcher_query_container_adjust_label_for_query("", 0);
    }
}

bool string_builder_insert_at_char(StringBuilder* sb, size_t char_pos, const char* text) {
    if (!sb || !text) return false;

    size_t byte_pos = utf8_char_to_byte_pos(sb->str, char_pos);
    return string_builder_insert(sb, byte_pos, text);
}

bool string_builder_erase_chars(StringBuilder* sb, size_t char_pos, size_t char_count) {
    if (!sb) return false;

    size_t start_byte = utf8_char_to_byte_pos(sb->str, char_pos);
    size_t end_byte = utf8_char_to_byte_pos(sb->str, char_pos + char_count);
    size_t byte_len = end_byte - start_byte;

    return string_builder_erase(sb, start_byte, byte_len);
}

bool state_change_cursor_position(int index) {
    const char* query = state_get_query();
    int query_char_len = utf8_char_count(query);

    int tmp = index < 0 ? 0 : (index > query_char_len ? query_char_len : index);

    if (tmp == state_cursor_positions[state_sf]) {
        return false;
    }

    state_cursor_positions[state_sf] = tmp;
    return true;
}

void state_append_query(const char* tail) {
    if (tail == NULL) return;

    string_builder_insert_at_char(state_queries[state_sf],
                                 state_cursor_positions[state_sf],
                                 tail);

    state_cursor_positions[state_sf] += utf8_char_count(tail);
    const char* q = state_get_query();

    size_t byte_pos = utf8_char_to_byte_pos(q, state_cursor_positions[state_sf]);
    bob_launcher_query_container_adjust_label_for_query(q, byte_pos);
    controller_start_search(q);
}

void state_char_left() {
    if (state_change_cursor_position(state_cursor_positions[state_sf] - 1)) {
        const char* q = state_get_query();
        size_t byte_pos = utf8_char_to_byte_pos(q, state_cursor_positions[state_sf]);
        bob_launcher_query_container_adjust_label_for_query(q, byte_pos);
    }
}

void state_char_right() {
    if (state_change_cursor_position(state_cursor_positions[state_sf] + 1)) {
        const char* q = state_get_query();
        size_t byte_pos = utf8_char_to_byte_pos(q, state_cursor_positions[state_sf]);
        bob_launcher_query_container_adjust_label_for_query(q, byte_pos);
    }
}

void state_word_left() {
    int next_word = state_find_next_word(false);
    if (state_change_cursor_position(next_word)) {
        const char* q = state_get_query();
        size_t byte_pos = utf8_char_to_byte_pos(q, state_cursor_positions[state_sf]);
        bob_launcher_query_container_adjust_label_for_query(q, byte_pos);
    }
}

void state_word_right() {
    int next_word = state_find_next_word(true);
    if (state_change_cursor_position(next_word)) {
        const char* q = state_get_query();
        size_t byte_pos = utf8_char_to_byte_pos(q, state_cursor_positions[state_sf]);
        bob_launcher_query_container_adjust_label_for_query(q, byte_pos);
    }
}

void state_line_begin() {
    if (state_change_cursor_position(0)) {
        bob_launcher_query_container_adjust_label_for_query(state_get_query(), 0);
    }
}

void state_line_end() {
    const char* q = state_get_query();
    int char_len = utf8_char_count(q);
    if (state_change_cursor_position(char_len)) {
        size_t byte_pos = utf8_char_to_byte_pos(q, char_len);
        bob_launcher_query_container_adjust_label_for_query(q, byte_pos);
    }
}

void state_delete_char_backward() {
    StringBuilder* sb = state_queries[state_sf];
    if (sb->len == 0) return;

    if (!state_change_cursor_position(state_cursor_positions[state_sf] - 1)) {
        return;
    }

    string_builder_erase_chars(sb, state_cursor_positions[state_sf], 1);
    const char* q = state_get_query();

    controller_start_search(q);

    size_t byte_pos = utf8_char_to_byte_pos(q, state_cursor_positions[state_sf]);
    bob_launcher_query_container_adjust_label_for_query(q, byte_pos);
}

void state_delete_char_forward() {
    StringBuilder* sb = state_queries[state_sf];
    if (utf8_char_count(sb->str) == state_cursor_positions[state_sf]) {
        return;
    }

    string_builder_erase_chars(sb, state_cursor_positions[state_sf], 1);
    const char* q = state_get_query();

    controller_start_search(q);

    size_t byte_pos = utf8_char_to_byte_pos(q, state_cursor_positions[state_sf]);
    bob_launcher_query_container_adjust_label_for_query(q, byte_pos);
}

void state_delete_word() {
    if (state_cursor_positions[state_sf] == 0) {
        return;
    }

    int next_word = state_find_next_word(false);
    if (next_word == state_cursor_positions[state_sf]) {
        return;
    }

    int char_length = state_cursor_positions[state_sf] - next_word;
    string_builder_erase_chars(
        state_queries[state_sf],
        next_word,
        char_length
    );

    state_change_cursor_position(next_word);
    controller_start_search(state_get_query());

    size_t byte_pos = utf8_char_to_byte_pos(state_get_query(), state_cursor_positions[state_sf]);
    bob_launcher_query_container_adjust_label_for_query(
        state_get_query(),
        byte_pos
    );
}

int state_find_next_word(bool right) {
    const char* text = state_get_query();
    int text_char_len = utf8_char_count(text);
    int char_index = state_cursor_positions[state_sf];

    if (right) {
        while (char_index < text_char_len) {
            size_t byte_pos = utf8_char_to_byte_pos(text, char_index);
            if (is_word_boundary(text[byte_pos])) {
                char_index++;
            } else {
                break;
            }
        }

        while (char_index < text_char_len) {
            size_t byte_pos = utf8_char_to_byte_pos(text, char_index);
            if (is_word_boundary(text[byte_pos])) {
                return char_index;
            }
            char_index++;
        }

        return text_char_len;
    } else {
        while (char_index > 0) {
            size_t byte_pos = utf8_char_to_byte_pos(text, char_index - 1);
            if (is_word_boundary(text[byte_pos])) {
                char_index--;
            } else {
                break;
            }
        }

        while (char_index > 0) {
            char_index--;
            size_t byte_pos = utf8_char_to_byte_pos(text, char_index);
            if (is_word_boundary(text[byte_pos])) {
                return char_index + 1;
            }
        }

        return 0;
    }
}

void state_change_category(BobLauncherSearchingFor what) {
    if (state_sf == what) return;

    bool should_update = what < state_sf;
    int new_event_id = events_increment();

    for (int i = what + 1; i < bob_launcher_SEARCHING_FOR_COUNT; i++) {
        state_update_provider(i, state_empty_provider(new_event_id), 0);

        string_builder_free(state_queries[i]);
        state_queries[i] = string_builder_new();
    }

    state_sf = (what == bob_launcher_SEARCHING_FOR_RESET) ?
                          bob_launcher_SEARCHING_FOR_SOURCES : what;

    const char* text = string_builder_get_str(state_queries[state_sf]);
    controller_start_search(text);
    int char_len = utf8_char_count(text);
    state_change_cursor_position(char_len);

    size_t byte_pos = utf8_char_to_byte_pos(text, state_cursor_positions[state_sf]);
    bob_launcher_query_container_adjust_label_for_query(text, byte_pos);

    if (should_update) {
        state_update_layout(CURRENT_CATEGORY);
    }
}


void state_cleanup() {
    // Free all resources
    for (int i = 0; i < bob_launcher_SEARCHING_FOR_COUNT; i++) {
        hashset_destroy(state_providers[i]);
        string_builder_free(state_queries[i]);
    }

    free(state_providers);
    free(state_queries);
    free(state_cursor_positions);
    free(state_selected_indices);

    state_providers = NULL;
    state_queries = NULL;
    state_cursor_positions = NULL;
    state_selected_indices = NULL;
}
