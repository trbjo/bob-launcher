#ifndef bob_launcher_CONTROLLER_H
#define bob_launcher_CONTROLLER_H

#include <stdbool.h>
#include "bob-launcher.h"

/* Public interface */

void controller_init();


BobLauncherMatch* controller_selected_match();
void controller_goto_match(int relative_index);
void controller_goto_match_abs(int abs_index);
void controller_on_drag_and_drop_done();
void controller_execute(bool should_hide);
void controller_start_search(const char* search_query);
bool controller_select_plugin(const char* plugin, const char* query);void controller_goto_match(int relative_index);
void controller_handle_key_release(unsigned int keyval, unsigned int state);
void controller_handle_key_press(unsigned int keyval, unsigned int state);
void controller_handle_focus_enter();
void controller_handle_focus_leave();

#endif /* bob_launcher_CONTROLLER_H */
