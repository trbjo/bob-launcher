#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <gtk/gtk.h>
#include "keybindings.h"
#include "bob-launcher.h"
#include "constants.h"
#include "string-utils.h"

#define keybindings_compute_hash(keyval, state) ((unsigned long)(keyval) | (((unsigned long)(state)) << 16))

static GHashTable* keybindings_key_to_command_press = NULL;
static GHashTable* keybindings_key_to_command_release = NULL;

const char* KEYBINDINGS_MODIFIER_KEYS[6] = {
    "Shift", "Control", "Alt", "Super", "Meta", "Hyper"
};

#define KEYBINDINGS_ALIASES_LENGTH 7
const char* ALIASES[KEYBINDINGS_ALIASES_LENGTH][2] = {
    {"Return", "Enter"},
    {"Escape", "Esc"},
    {"Delete", "Del"},
    {"BackSpace", "Back"},
    {"Page_Up", "PgUp"},
    {"Page_Down", "PgDn"},
    {"Tab", "ISO_Left_Tab"},
};

const struct {
    int command;
    const char* name;
} command_map[] = {
    { KEYBINDINGS_COMMAND_INVALID_COMMAND, "invalid-command" }, // == 0; must be first
    { KEYBINDINGS_COMMAND_CLEAR_SEARCH_OR_HIDE, "clear-search-or-hide" },
    { KEYBINDINGS_COMMAND_DELETE_CHAR_BACKWARD, "delete-char-backward" },
    { KEYBINDINGS_COMMAND_DELETE_CHAR_FORWARD, "delete-char-forward" },
    { KEYBINDINGS_COMMAND_DELETE_WORD, "delete-word" },
    { KEYBINDINGS_COMMAND_CHAR_LEFT, "char-left" },
    { KEYBINDINGS_COMMAND_CHAR_RIGHT, "char-right" },
    { KEYBINDINGS_COMMAND_WORD_LEFT, "word-left" },
    { KEYBINDINGS_COMMAND_WORD_RIGHT, "word-right" },
    { KEYBINDINGS_COMMAND_LINE_BEGIN, "line-begin" },
    { KEYBINDINGS_COMMAND_LINE_END, "line-end" },
    { KEYBINDINGS_COMMAND_EXECUTE, "execute" },
    { KEYBINDINGS_COMMAND_EXECUTE_WITHOUT_HIDE, "execute-without-hide" },
    { KEYBINDINGS_COMMAND_FIRST_MATCH, "first-match" },
    { KEYBINDINGS_COMMAND_LAST_MATCH, "last-match" },
    { KEYBINDINGS_COMMAND_MATCH_1, "match-1" },
    { KEYBINDINGS_COMMAND_MATCH_2, "match-2" },
    { KEYBINDINGS_COMMAND_MATCH_3, "match-3" },
    { KEYBINDINGS_COMMAND_MATCH_4, "match-4" },
    { KEYBINDINGS_COMMAND_MATCH_5, "match-5" },
    { KEYBINDINGS_COMMAND_MATCH_6, "match-6" },
    { KEYBINDINGS_COMMAND_MATCH_7, "match-7" },
    { KEYBINDINGS_COMMAND_MATCH_8, "match-8" },
    { KEYBINDINGS_COMMAND_MATCH_9, "match-9" },
    { KEYBINDINGS_COMMAND_MATCH_10, "match-10" },
    { KEYBINDINGS_COMMAND_NEXT_MATCH, "next-match" },
    { KEYBINDINGS_NEXT_PANE_1, "next-pane-1"},
    { KEYBINDINGS_NEXT_PANE_2, "next-pane-2"},
    { KEYBINDINGS_NEXT_PANE_3, "next-pane-3"},
    { KEYBINDINGS_NEXT_PANE_4, "next-pane-4"},
    { KEYBINDINGS_NEXT_PANE_5, "next-pane-5"},
    { KEYBINDINGS_NEXT_PANE_6, "next-pane-6"},
    { KEYBINDINGS_NEXT_PANE_7, "next-pane-7"},
    { KEYBINDINGS_NEXT_PANE_8, "next-pane-8"},
    { KEYBINDINGS_NEXT_PANE_9, "next-pane-9"},
    { KEYBINDINGS_NEXT_PANE_10, "next-pane-10"},
    { KEYBINDINGS_COMMAND_NEXT_PANE, "next-pane" },
    { KEYBINDINGS_COMMAND_PAGE_DOWN, "page-down" },
    { KEYBINDINGS_COMMAND_PAGE_UP, "page-up" },
    { KEYBINDINGS_COMMAND_PASTE, "paste" },
    { KEYBINDINGS_COMMAND_PASTE_SELECTION, "paste-selection" },
    { KEYBINDINGS_COMMAND_PREV_MATCH, "prev-match" },
    { KEYBINDINGS_COMMAND_PREV_PANE, "prev-pane" },
    // the following commands may be run when executing and the ui is otherwise "frozen"
    { KEYBINDINGS_COMMAND_QUIT, "quit" },
    { KEYBINDINGS_COMMAND_ACTIVATE, "activate" },
    { KEYBINDINGS_COMMAND_HIGHLIGHT_MATCH, "highlight-match" },
    { KEYBINDINGS_COMMAND_HIGHLIGHT_MATCH_RELEASE, "highlight-match-release" },
    { 0, NULL }
};

static void array_add(char*** array, int* length, int* size, char* value) {
    if (*length == *size) {
        *size = (*size == 0) ? 4 : (*size * 2);

        char** new_array = (char**)realloc(*array, ((*size) + 1) * sizeof(char*));
        if (new_array) {
            *array = new_array;
        } else {
            free(value);
            return;
        }
    }
    (*array)[(*length)++] = value;
    (*array)[*length] = NULL;
}

static void str_array_free(char** array) {
    if (!array) return;

    for (int i = 0; array[i]; i++) {
        free(array[i]);
    }
    free(array);
}

char** keybindings_generate_key_variants(const char* accel, int* result_length) {
    if (!accel) return NULL;

    int variants_length = 0;
    int variants_size = 0;
    char** variants = NULL;
    array_add(&variants, &variants_length, &variants_size, strdup(accel));

    char** key_parts = str_split(accel, "+", 0);
    int key_parts_length = 0;
    while (key_parts[key_parts_length]) key_parts_length++;

    if (key_parts_length > 0) {
        char* main_key = key_parts[key_parts_length - 1];
        for (int i = 0; i < KEYBINDINGS_ALIASES_LENGTH; i++) {
            if (strcmp(ALIASES[i][0], main_key) == 0) {
                array_add(&variants, &variants_length, &variants_size, replace(accel, main_key, ALIASES[i][1]));
                break;
            }
        }

        int modifier_variants_length = 0;
        int modifier_variants_size = 0;
        char** modifier_variants = NULL;
        array_add(&modifier_variants, &modifier_variants_length, &modifier_variants_size, strdup(""));

        for (int i = 0; i < 6; i++) {
            char mod_pattern[32];
            snprintf(mod_pattern, sizeof(mod_pattern), "<%s>", KEYBINDINGS_MODIFIER_KEYS[i]);

            if (string_contains(accel, mod_pattern)) {
                int new_variants_length = 0;
                int new_variants_size = 0;
                char** new_variants = NULL;

                for (int j = 0; j < modifier_variants_length; j++) {
                    char* left_mod = str_format("<%s_L>", KEYBINDINGS_MODIFIER_KEYS[i]);
                    char* right_mod = str_format("<%s_R>", KEYBINDINGS_MODIFIER_KEYS[i]);

                    array_add(&new_variants, &new_variants_length, &new_variants_size,
                              str_concat(modifier_variants[j], mod_pattern));
                    array_add(&new_variants, &new_variants_length, &new_variants_size,
                              str_concat(modifier_variants[j], left_mod));
                    array_add(&new_variants, &new_variants_length, &new_variants_size,
                              str_concat(modifier_variants[j], right_mod));

                    free(left_mod);
                    free(right_mod);
                }

                str_array_free(modifier_variants);
                modifier_variants = new_variants;
                modifier_variants_length = new_variants_length;
                modifier_variants_size = new_variants_size;
            }
        }

        if (modifier_variants_length > 1) {
            int combined_variants_length = 0;
            int combined_variants_size = 0;
            char** combined_variants = NULL;

            for (int i = 0; i < modifier_variants_length; i++) {
                for (int j = 0; j < variants_length; j++) {
                    array_add(&combined_variants, &combined_variants_length, &combined_variants_size,
                              str_concat(modifier_variants[i], variants[j]));
                }
            }

            str_array_free(variants);
            variants = combined_variants;
            variants_length = combined_variants_length;
            variants_size = combined_variants_size;
        }

        str_array_free(modifier_variants);
    }

    str_array_free(key_parts);

    if (result_length)
        *result_length = variants_length;

    return variants;
}

static void keybindings_register_accelerator(const char* accel, KeybindingsCommand command, GHashTable* command_map) {
    int variants_length = 0;
    char** variants = keybindings_generate_key_variants(accel, &variants_length);

    for (int i = 0; i < variants_length; i++) {
        unsigned int keyval = 0;
        GdkModifierType state = 0;

        gtk_accelerator_parse(variants[i], &keyval, &state);
        unsigned int normalized_keyval = gdk_keyval_to_lower(keyval);
        unsigned long hash = keybindings_compute_hash(normalized_keyval, state);

        g_hash_table_insert(command_map, GSIZE_TO_POINTER(hash), GINT_TO_POINTER(command));
    }

    for (int i = 0; i < variants_length; i++) {
        free(variants[i]);
    }
    free(variants);
}

static void keybindings_update_all(GSettings* keybindings_settings) {
    g_hash_table_remove_all(keybindings_key_to_command_press);
    g_hash_table_remove_all(keybindings_key_to_command_release);

    // Skip the first entry (INVALID_COMMAND) and the last NULL entry
    for (int i = 1; command_map[i].name != NULL; i++) {
        KeybindingsCommand command = command_map[i].command;
        const char* key_name = command_map[i].name;

        char** accels = g_settings_get_strv(keybindings_settings, key_name);

        for (int j = 0; accels && accels[j]; j++) {
            if (str_has_suffix(key_name, "-release")) {
                keybindings_register_accelerator(accels[j], command, keybindings_key_to_command_release);
            } else {
                keybindings_register_accelerator(accels[j], command, keybindings_key_to_command_press);
            }
        }

        g_strfreev(accels);
    }
}

static void _keybindings_update_all_g_settings_changed(GSettings* _sender, const char* key, gpointer self) {
    keybindings_update_all(_sender);
}

KeybindingsCommand keybindings_command_from_key_press(unsigned int keyval, GdkModifierType state) {
    unsigned int normalized_keyval = gdk_keyval_to_lower(keyval);
    unsigned long hash = keybindings_compute_hash(normalized_keyval, state);
    gpointer value = g_hash_table_lookup(keybindings_key_to_command_press, GSIZE_TO_POINTER(hash));
    return (KeybindingsCommand)GPOINTER_TO_INT(value);
}

KeybindingsCommand keybindings_command_from_key_release(unsigned int keyval, GdkModifierType state) {
    unsigned int normalized_keyval = gdk_keyval_to_lower(keyval);
    unsigned long hash = keybindings_compute_hash(normalized_keyval, state);
    gpointer value = g_hash_table_lookup(keybindings_key_to_command_release, GSIZE_TO_POINTER(hash));
    return (KeybindingsCommand)GPOINTER_TO_INT(value);
}

void keybindings_cleanup(void) {
    g_hash_table_unref(keybindings_key_to_command_press);
    g_hash_table_unref(keybindings_key_to_command_release);
}

void keybindings_initialize() {
    keybindings_key_to_command_press = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);
    keybindings_key_to_command_release = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);

    GSettings* keybindings_settings = g_settings_new(BOB_LAUNCHER_APP_ID ".keybindings");
    keybindings_update_all(keybindings_settings);
    g_signal_connect(keybindings_settings, "changed", (GCallback)_keybindings_update_all_g_settings_changed, NULL);
}
