#ifndef HOTKEYS_H
#define HOTKEYS_H

#include "common.h"
#include <X11/Xlib.h>
#include <X11/keysym.h>

typedef enum {
    HOTKEY_MOVE_START = 0,
    HOTKEY_MOVE_END,
    HOTKEY_HISTORY_PREV,
    HOTKEY_HISTORY_NEXT,
    HOTKEY_DEL_TO_START,
    HOTKEY_DEL_TO_END,
    HOTKEY_DEL_PREV_WORD,
    HOTKEY_CLEAR_SCREEN,
    HOTKEY_EXIT_SHELL,
    HOTKEY_COPY,
    HOTKEY_PASTE,
    HOTKEY_SUSPEND,
    HOTKEY_COUNT
} HotkeyAction;

typedef struct {
    KeySym keysym;
    unsigned int mod_mask;
} HotkeyBinding;

void hotkeys_init(HotkeyBinding bindings[HOTKEY_COUNT]);
void hotkeys_load_config(HotkeyBinding bindings[HOTKEY_COUNT]);
int hotkeys_match(const HotkeyBinding bindings[HOTKEY_COUNT], KeySym sym, unsigned int state);

#endif
