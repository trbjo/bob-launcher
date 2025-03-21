#ifndef KEYBINDINGS_H
#define KEYBINDINGS_H

typedef enum {
    KEYBINDINGS_COMMAND_INVALID_COMMAND,
    KEYBINDINGS_COMMAND_ACTIVATE,
    KEYBINDINGS_COMMAND_CLEAR_SEARCH_OR_HIDE,
    KEYBINDINGS_COMMAND_DELETE_CHAR_BACKWARD,
    KEYBINDINGS_COMMAND_DELETE_CHAR_FORWARD,
    KEYBINDINGS_COMMAND_DELETE_WORD,
    KEYBINDINGS_COMMAND_CHAR_LEFT,
    KEYBINDINGS_COMMAND_CHAR_RIGHT,
    KEYBINDINGS_COMMAND_WORD_LEFT,
    KEYBINDINGS_COMMAND_WORD_RIGHT,
    KEYBINDINGS_COMMAND_LINE_BEGIN,
    KEYBINDINGS_COMMAND_LINE_END,
    KEYBINDINGS_COMMAND_EXECUTE,
    KEYBINDINGS_COMMAND_EXECUTE_WITHOUT_HIDE,
    KEYBINDINGS_COMMAND_FIRST_MATCH,
    KEYBINDINGS_COMMAND_HIGHLIGHT_MATCH,
    KEYBINDINGS_COMMAND_HIGHLIGHT_MATCH_RELEASE,
    KEYBINDINGS_COMMAND_LAST_MATCH,
    KEYBINDINGS_COMMAND_MATCH_1,
    KEYBINDINGS_COMMAND_MATCH_2,
    KEYBINDINGS_COMMAND_MATCH_3,
    KEYBINDINGS_COMMAND_MATCH_4,
    KEYBINDINGS_COMMAND_MATCH_5,
    KEYBINDINGS_COMMAND_MATCH_6,
    KEYBINDINGS_COMMAND_MATCH_7,
    KEYBINDINGS_COMMAND_MATCH_8,
    KEYBINDINGS_COMMAND_MATCH_9,
    KEYBINDINGS_COMMAND_MATCH_10,
    KEYBINDINGS_COMMAND_NEXT_MATCH,
    KEYBINDINGS_COMMAND_NEXT_PANE,
    KEYBINDINGS_COMMAND_PAGE_DOWN,
    KEYBINDINGS_COMMAND_PAGE_UP,
    KEYBINDINGS_COMMAND_PASTE,
    KEYBINDINGS_COMMAND_PASTE_SELECTION,
    KEYBINDINGS_COMMAND_PREV_MATCH,
    KEYBINDINGS_COMMAND_PREV_PANE,
    KEYBINDINGS_COMMAND_QUIT,
    KEYBINDINGS_COMMAND_SHOW_SETTINGS,
} KeybindingsCommand;

void keybindings_initialize(void);
KeybindingsCommand keybindings_command_from_key_press(unsigned int keyval, GdkModifierType state);
KeybindingsCommand keybindings_command_from_key_release(unsigned int keyval, GdkModifierType state);

#endif /* KEYBINDINGS_H */
