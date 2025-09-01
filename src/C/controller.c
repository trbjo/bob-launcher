#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include "state.h"
#include "hashset.h"
#include "thread-manager.h"
#include "events.h"
#include "string-utils.h"
#include "bob-launcher.h"
#include "data-sink-sources.h"
#include "keybindings.h"

typedef struct _BobLauncherAppSettingsUI BobLauncherAppSettingsUI;
typedef struct _BobLauncherAppSettings BobLauncherAppSettings;
typedef struct _BobLauncherMatchRow BobLauncherMatchRow;
typedef struct _BobLauncherAppSettingsClass BobLauncherAppSettingsClass;
typedef struct _BobLauncherAppSettingsPlugins BobLauncherAppSettingsPlugins;
typedef struct _BobLauncherLauncherWindow BobLauncherLauncherWindow;
typedef struct _BobLauncherAppSettingsUI BobLauncherAppSettingsUI;

struct _BobLauncherMatchRow {
    GtkWidget parent_instance;
    void* priv;
    gint abs_index;
    gint event_id;
};

typedef struct {
    char* search_query;
    BobLauncherSearchBase* plg;
    int event_id;
} TimeoutData;

typedef void (*TaskFunc)(void *data);

extern gboolean bob_launcher_app_settings_ui_get_hide_after_dnd_success (BobLauncherAppSettingsUI* self);

extern int data_sink_find_plugin_by_name (const char* query);
extern int bob_launcher_result_box_box_size;
extern int bob_launcher_result_box_visible_size;

extern void bob_launcher_query_container_adjust_label_for_query (const char* text, int cursor_position);
extern void bob_launcher_scroll_controller_reset (void);

extern void bob_launcher_app_open_settings();
extern BobLauncherAppSettings* bob_launcher_app_settings_get_default (void);
extern BobLauncherAppSettingsPlugins* bob_launcher_app_settings_get_plugins (BobLauncherAppSettings* self);
extern BobLauncherAppSettingsUI* bob_launcher_app_settings_get_ui (BobLauncherAppSettings* self);
extern KeybindingsCommand keybindings_command_from_key_press (unsigned int keyval, GdkModifierType state);
extern KeybindingsCommand keybindings_command_from_key_release (unsigned int keyval, GdkModifierType state);
extern BobLauncherLauncherWindow* bob_launcher_app_main_win;
extern BobLauncherMatchRow** bob_launcher_result_box_row_pool;

HashSet* data_sink_search_for_actions (const char* query, BobLauncherMatch* m, int event_id);
HashSet* data_sink_search_for_plugins (const char* query, int event_id);
HashSet* data_sink_search_for_targets (const char* query, BobLauncherMatch* a, int event_id);

typedef struct {
    BobLauncherMatch* source;
    BobLauncherMatch* action;
    BobLauncherMatch* target;
    bool should_hide;
} ExecuteData;

bool controller_select_plugin(const char* plugin, const char* query) {
    if (!state_is_in_initial_state()) return false;

    int index = data_sink_find_plugin_by_name(plugin);
    if (index == -1) return false;

    int new_event_id = events_increment();
    HashSet* result_provider = data_sink_search_for_plugins("", new_event_id);
    state_update_provider(bob_launcher_SEARCHING_FOR_PLUGINS, result_provider, index);

    state_append_query(query != NULL ? query : "");

    return true;
}


BobLauncherMatch* controller_selected_match() {
    return hashset_get_match_at(
        state_providers[state_sf],
        state_selected_indices[state_sf]
    );
}

void controller_goto_match(int relative_index) {
    HashSet* rp = state_providers[state_sf];
    int abs_index = relative_index + state_selected_indices[state_sf];
    abs_index = MAX(0, MIN(abs_index, rp->size - 1));
    state_selected_indices[state_sf] = abs_index;
}

void controller_goto_match_abs(int abs_index) {
    int total_size = state_providers[state_sf]->size;
    if (total_size <= 0) return;

    // Handle negative indices and wrap around
    abs_index = ((abs_index % total_size) + total_size) % total_size;
    state_selected_indices[state_sf] = abs_index;
}

static inline void controller_page(bool down) {
    int direction = down ? 1 : -1;
    controller_goto_match(direction * bob_launcher_result_box_box_size);
}

void controller_on_drag_and_drop_done() {
    BobLauncherAppSettings* settings = bob_launcher_app_settings_get_default();
    BobLauncherAppSettingsUI* ui = bob_launcher_app_settings_get_ui (settings);
    if (bob_launcher_app_settings_ui_get_hide_after_dnd_success(ui)) {
        gtk_widget_set_visible((GtkWidget*)bob_launcher_app_main_win, false);
    }
}

void controller_handle_focus_enter() {
    gtk_widget_unset_state_flags((GtkWidget*)bob_launcher_app_main_win, GTK_STATE_FLAG_BACKDROP);
}

void controller_handle_focus_leave() {
    gtk_widget_remove_css_class((GtkWidget*)bob_launcher_app_main_win, "highlighted");
    gtk_widget_set_state_flags((GtkWidget*)bob_launcher_app_main_win, GTK_STATE_FLAG_BACKDROP, false);
    gtk_widget_set_cursor((GtkWidget*)bob_launcher_app_main_win, NULL);
}

static bool bob_launcher_app_hide_window(void* user_data) {
    gtk_widget_set_visible ((GtkWidget*) bob_launcher_app_main_win, false);
    return false;
}

static bool state_update_layout_idle(void* user_data) {
    state_update_layout ((BobLauncherSearchingFor) -999);
    return false;
}

static void* controller_execute_async(void* data) {
    ExecuteData* exec_data = (ExecuteData*)data;

    BobLauncherAction* action = (BobLauncherAction*)exec_data->action;
    bool result = bob_launcher_action_execute(action, exec_data->source, exec_data->target);

    if (result && exec_data->should_hide) {
        g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, (GSourceFunc)bob_launcher_app_hide_window, NULL, NULL);
    } else {
        g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, (GSourceFunc)state_update_layout_idle, NULL, NULL);
    }

    free(exec_data);
    return NULL;
}

void controller_execute(bool should_hide) {
    if (hashset_get_match_at(state_providers[state_sf],
                             state_selected_indices[state_sf]) == NULL) {
        return;
    }

    if (state_sf == bob_launcher_SEARCHING_FOR_PLUGINS) {
        state_change_category(bob_launcher_SEARCHING_FOR_SOURCES);
        return;
    }

    BobLauncherMatch* source = state_selected_source();
    if (source == NULL) return;

    if (state_sf < bob_launcher_SEARCHING_FOR_ACTIONS) {
        HashSet* result_provider = data_sink_search_for_actions("", source, events_get());
        state_update_provider(bob_launcher_SEARCHING_FOR_ACTIONS, result_provider, 0);
    }

    BobLauncherMatch* action = state_selected_action();
    if (action == NULL) return;

    if (BOB_LAUNCHER_IS_ACTION_TARGET(action) && state_sf < bob_launcher_SEARCHING_FOR_TARGETS) {
        HashSet* result_provider = data_sink_search_for_targets("", action, events_increment());

        if (result_provider->size == 0) {
            hashset_destroy(result_provider);
            return;
        }

        state_update_provider(bob_launcher_SEARCHING_FOR_TARGETS, result_provider, 0);
        state_update_layout(bob_launcher_SEARCHING_FOR_TARGETS);
        bob_launcher_query_container_adjust_label_for_query("", 0);
    }

    BobLauncherMatch* target = state_selected_target();
    if (target == NULL && BOB_LAUNCHER_IS_ACTION_TARGET(action)) return;

    // Prepare data for async execution
    ExecuteData* exec_data = (ExecuteData*)malloc(sizeof(ExecuteData));
    if (exec_data == NULL) return;

    exec_data->source = source;
    exec_data->action = action;
    exec_data->target = target;
    exec_data->should_hide = should_hide;

    thread_pool_run((TaskFunc)controller_execute_async, exec_data, NULL);
}

static gboolean search_update_timeout_callback(gpointer user_data) {
    TimeoutData* data = (TimeoutData*)user_data;

    if (!events_ok(data->event_id)) {
        /* Clean up and return false to stop the timeout */
        free(data->search_query);
        g_object_unref(data->plg);
        free(data);
        return false;
    }

    data->event_id = events_increment();

    data_sink_sources_execute_search(
        data->search_query,
        data->plg,
        data->event_id,
        false
    );

    return true;
}

static void setup_search_timeout(const char* search_query, BobLauncherSearchBase* plg, int event_id, int interval_ms) {
    TimeoutData* data = (TimeoutData*)malloc(sizeof(TimeoutData));
    if (!data) return;

    data->search_query = strdup(search_query);
    data->plg = g_object_ref(plg);
    data->event_id = event_id;

    g_timeout_add_full(
        G_PRIORITY_DEFAULT,           /* Default priority */
        interval_ms,                  /* Interval in milliseconds */
        search_update_timeout_callback, /* Callback function */
        data,                         /* User data */
        NULL                          /* No GDestroyNotify needed as we clean up in the callback */
    );
}

void controller_start_search(const char* search_query) {
    int event_id = events_increment();

    switch (state_sf) {
        case bob_launcher_SEARCHING_FOR_PLUGINS: {
            HashSet* result_provider = data_sink_search_for_plugins(search_query, event_id);
            state_update_provider(bob_launcher_SEARCHING_FOR_PLUGINS, result_provider, 0);
            state_update_layout(-999);
            break;
        }

        case bob_launcher_SEARCHING_FOR_SOURCES: {
            BobLauncherSearchBase* plg = (BobLauncherSearchBase*)state_selected_plugin();
            if (plg == NULL && strcmp(search_query, "") == 0) {
                HashSet* empty_provider = state_empty_provider(event_id);
                state_update_provider(bob_launcher_SEARCHING_FOR_SOURCES, empty_provider, 0);
                state_update_layout(-999);
                return;
            }

            data_sink_sources_execute_search(search_query, plg, event_id, true);

            if (plg != NULL && bob_launcher_search_base_get_update_interval(plg) > 0) {
                int interval_ms = bob_launcher_search_base_get_update_interval(plg) / 1000;
                setup_search_timeout(search_query, plg, event_id, interval_ms);
            }
            break;
        }

        case bob_launcher_SEARCHING_FOR_ACTIONS: {
            BobLauncherMatch* source = state_selected_source();
            if (source != NULL) {
                HashSet* rs_array = data_sink_search_for_actions(search_query, source, event_id);
                state_update_provider(bob_launcher_SEARCHING_FOR_ACTIONS, rs_array, 0);
            }
            state_update_layout(-999);
            break;
        }

        case bob_launcher_SEARCHING_FOR_TARGETS: {
            BobLauncherMatch* action = state_selected_action();
            HashSet* target_provider = data_sink_search_for_targets(search_query, action, event_id);
            state_update_provider(bob_launcher_SEARCHING_FOR_TARGETS, target_provider, 0);
            state_update_layout(-999);
            break;
        }

        default:
            break;
    }
}

static void controller_clipboard_callback(GdkClipboard* clipboard,
                                                  GAsyncResult* result,
                                                  gpointer user_data) {
    GError* error = NULL;
    char* text = gdk_clipboard_read_text_finish(clipboard, result, &error);

    if (!error && text != NULL && strcmp(text, "") != 0) {
        state_append_query(text);
    }

    g_free(text);
    if (error) g_error_free(error);
}

bool controller_handle_command(int command) {
    switch (command) {
        case KEYBINDINGS_COMMAND_ACTIVATE:
            gtk_widget_set_visible((GtkWidget*)bob_launcher_app_main_win, !gtk_widget_get_visible((GtkWidget*)bob_launcher_app_main_win));
            return true;

        case KEYBINDINGS_COMMAND_CLEAR_SEARCH_OR_HIDE:
            if (state_is_in_initial_state()) {
                gtk_widget_set_visible((GtkWidget*)bob_launcher_app_main_win, false);
            } else if (strcmp(state_get_query(), "") != 0) {
                state_delete_line();
            } else {
                state_change_category(state_sf - 1);
            }
            return true;

        case KEYBINDINGS_COMMAND_DELETE_CHAR_BACKWARD:
            state_delete_char_backward();
            return true;

        case KEYBINDINGS_COMMAND_DELETE_CHAR_FORWARD:
            state_delete_char_forward();
            return true;

        case KEYBINDINGS_COMMAND_DELETE_WORD:
            state_delete_word();
            return true;

        case KEYBINDINGS_COMMAND_EXECUTE:
        case KEYBINDINGS_COMMAND_EXECUTE_WITHOUT_HIDE:
            controller_execute(command == KEYBINDINGS_COMMAND_EXECUTE);
            return true;

        case KEYBINDINGS_COMMAND_FIRST_MATCH:
            controller_goto_match_abs(0);
            state_update_layout(-999);
            return true;

        case KEYBINDINGS_COMMAND_HIGHLIGHT_MATCH:
            gtk_widget_add_css_class ((GtkWidget*)bob_launcher_app_main_win, "highlighted");
            return true;

        case KEYBINDINGS_COMMAND_HIGHLIGHT_MATCH_RELEASE:
            gtk_widget_remove_css_class ((GtkWidget*)bob_launcher_app_main_win, "highlighted");
            return true;

        case KEYBINDINGS_COMMAND_LAST_MATCH:
            controller_goto_match_abs(-1);
            state_update_layout(-999);
            return true;

        case KEYBINDINGS_COMMAND_CHAR_LEFT:
            state_char_left();
            return true;

        case KEYBINDINGS_COMMAND_CHAR_RIGHT:
            state_char_right();
            return true;

        case KEYBINDINGS_COMMAND_WORD_LEFT:
            state_word_left();
            return true;

        case KEYBINDINGS_COMMAND_WORD_RIGHT:
            state_word_right();
            return true;

        case KEYBINDINGS_COMMAND_LINE_BEGIN:
            state_line_begin();
            return true;

        case KEYBINDINGS_COMMAND_LINE_END:
            state_line_end();
            return true;

        case KEYBINDINGS_COMMAND_MATCH_1:
        case KEYBINDINGS_COMMAND_MATCH_2:
        case KEYBINDINGS_COMMAND_MATCH_3:
        case KEYBINDINGS_COMMAND_MATCH_4:
        case KEYBINDINGS_COMMAND_MATCH_5:
        case KEYBINDINGS_COMMAND_MATCH_6:
        case KEYBINDINGS_COMMAND_MATCH_7:
        case KEYBINDINGS_COMMAND_MATCH_8:
        case KEYBINDINGS_COMMAND_MATCH_9:
        case KEYBINDINGS_COMMAND_MATCH_10: {
            int relative_index = command - KEYBINDINGS_COMMAND_MATCH_1;
            if (relative_index >= bob_launcher_result_box_visible_size) return true;
            BobLauncherMatchRow* row = bob_launcher_result_box_row_pool[relative_index];
            int abs_index = row->abs_index;
            controller_goto_match_abs(abs_index);
            state_update_layout ((BobLauncherSearchingFor) -999); // update the UI
            controller_execute(true);
            return true;
        }

        case KEYBINDINGS_COMMAND_NEXT_MATCH:
            controller_goto_match(1);
            state_update_layout(-999);
            return true;

        case KEYBINDINGS_COMMAND_NEXT_PANE: {
            if (state_is_in_initial_state()) {
                state_change_category(bob_launcher_SEARCHING_FOR_PLUGINS);
            } else {
                BobLauncherMatch* cm = controller_selected_match();
                int direction = 1;

                if (cm == NULL ||
                    (state_sf > bob_launcher_SEARCHING_FOR_SOURCES &&
                     (state_sf != bob_launcher_SEARCHING_FOR_ACTIONS ||
                      !BOB_LAUNCHER_IS_ACTION_TARGET(cm)))) {
                    direction = -1;
                }

                state_change_category(state_sf + direction);

            }
            return true;
        }

        case KEYBINDINGS_COMMAND_PAGE_DOWN:
            controller_page(true);
            state_update_layout(-999);
            return true;

        case KEYBINDINGS_COMMAND_PAGE_UP:
            controller_page(false);
            state_update_layout(-999);
            return true;

        case KEYBINDINGS_COMMAND_PASTE:
        case KEYBINDINGS_COMMAND_PASTE_SELECTION: {
            GdkDisplay* display = gdk_display_get_default();
            GdkClipboard* cb = command == KEYBINDINGS_COMMAND_PASTE ?
                gdk_display_get_clipboard(display) :
                gdk_display_get_primary_clipboard(display);

            gdk_clipboard_read_text_async(cb, NULL,
                (GAsyncReadyCallback)controller_clipboard_callback,
                NULL);

            return true;
        }

        case KEYBINDINGS_COMMAND_PREV_MATCH:

            controller_goto_match(-1);
            state_update_layout(-999);
            return true;

        case KEYBINDINGS_COMMAND_PREV_PANE: {
            state_change_category(state_sf - 1);
            return true;
        }

        case KEYBINDINGS_COMMAND_QUIT:
            gtk_window_close ((GtkWindow*) bob_launcher_app_main_win);
            return true;

        case KEYBINDINGS_COMMAND_SHOW_SETTINGS:
            bob_launcher_app_open_settings();
            return true;

        case KEYBINDINGS_COMMAND_SNEAK_PEEK:
            bob_launcher_app_open_settings();
            return true;

        case KEYBINDINGS_COMMAND_SNEAK_PEEK_RELEASE:
            bob_launcher_app_open_settings();
            return true;

        case KEYBINDINGS_COMMAND_INVALID_COMMAND:
        default:
            return false;
    }
}

void controller_handle_key_release(unsigned int keyval, GdkModifierType state) {
    controller_handle_command(keybindings_command_from_key_release(keyval, state));
}

void controller_handle_key_press(unsigned int keyval, GdkModifierType state) {
    if (!controller_handle_command(keybindings_command_from_key_press(keyval, state))) {
        char ch = gdk_keyval_to_unicode(keyval);
        if (g_unichar_isprint(ch)) {
            char str[8] = {0}; // UTF-8 can be up to 6 bytes + null terminator
            int len = g_unichar_to_utf8(ch, str);
            str[len] = '\0';
            state_append_query(str);
        }
    }
    bob_launcher_scroll_controller_reset();
}
