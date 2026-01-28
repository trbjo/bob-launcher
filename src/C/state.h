#pragma once

#include <stdbool.h>
#include "hashset.h"
#include "bob-launcher.h"

typedef enum {
    CURRENT_CATEGORY = -999,
    bob_launcher_SEARCHING_FOR_CURRENT = -999,
    bob_launcher_SEARCHING_FOR_RESET = -1,
    bob_launcher_SEARCHING_FOR_PLUGINS = 0,
    bob_launcher_SEARCHING_FOR_SOURCES = 1,
    bob_launcher_SEARCHING_FOR_ACTIONS = 2,
    bob_launcher_SEARCHING_FOR_TARGETS = 3,
    bob_launcher_SEARCHING_FOR_COUNT = 4
} BobLauncherSearchingFor;

typedef struct {
    char* str;
    size_t len;
    size_t capacity;
} StringBuilder;

/* State variables declarations */
extern BobLauncherSearchingFor state_sf;
extern HashSet** state_providers;
extern int state_providers_length1;
extern int* state_selected_indices;
extern int state_selected_indices_length1;
extern StringBuilder** state_queries;
extern int state_queries_length1;
int state_get_cursor_position(void);

/* StringBuilder functions */
StringBuilder* string_builder_new();
void string_builder_free(StringBuilder* sb);
bool string_builder_ensure_capacity(StringBuilder* sb, size_t new_len);
bool string_builder_insert_at_char(StringBuilder* sb, size_t char_pos, const char* text);
bool string_builder_erase_chars(StringBuilder* sb, size_t char_pos, size_t char_count);

/* State management functions */
HashSet* state_empty_provider(int event_id);
void state_initialize();
bool state_is_in_initial_state();
const char* state_get_query();
HashSet* state_current_provider();
BobLauncherMatch* state_selected_plugin();
BobLauncherMatch* state_selected_source();
BobLauncherMatch* state_selected_action();
BobLauncherMatch* state_selected_target();
int state_update_provider(BobLauncherSearchingFor what, HashSet* new_provider, int selected_index);
void state_update_layout(BobLauncherSearchingFor what);
void state_reset();
void state_change_category(BobLauncherSearchingFor what);
bool state_change_cursor_position(int index);
void state_delete_line();
void state_append_query(const char* tail);
void state_char_left();
void state_char_right();
void state_word_left();
void state_word_right();
void state_line_begin();
void state_line_end();
void state_delete_char_backward();
void state_delete_char_forward();
void state_delete_word();
int state_find_next_word(bool right);
void state_cleanup();
