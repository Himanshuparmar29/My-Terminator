#include "hotkeys.h"
#include <ctype.h>

static const char *action_names[HOTKEY_COUNT] = {
    "move_start",
    "move_end",
    "history_prev",
    "history_next",
    "del_to_start",
    "del_to_end",
    "del_prev_word",
    "clear_screen",
    "exit_shell",
    "copy",
    "paste",
    "suspend"
};

void hotkeys_init(HotkeyBinding bindings[HOTKEY_COUNT]) {
    bindings[HOTKEY_MOVE_START] = (HotkeyBinding){ XK_a, ControlMask };
    bindings[HOTKEY_MOVE_END] = (HotkeyBinding){ XK_e, ControlMask };
    bindings[HOTKEY_HISTORY_PREV] = (HotkeyBinding){ XK_Up, 0 };
    bindings[HOTKEY_HISTORY_NEXT] = (HotkeyBinding){ XK_Down, 0 };
    bindings[HOTKEY_DEL_TO_START] = (HotkeyBinding){ XK_u, ControlMask };
    bindings[HOTKEY_DEL_TO_END] = (HotkeyBinding){ XK_k, ControlMask };
    bindings[HOTKEY_DEL_PREV_WORD] = (HotkeyBinding){ XK_w, ControlMask };
    bindings[HOTKEY_CLEAR_SCREEN] = (HotkeyBinding){ XK_l, ControlMask };
    bindings[HOTKEY_EXIT_SHELL] = (HotkeyBinding){ XK_d, ControlMask };
    bindings[HOTKEY_COPY] = (HotkeyBinding){ XK_c, ControlMask | ShiftMask };
    bindings[HOTKEY_PASTE] = (HotkeyBinding){ XK_v, ControlMask | ShiftMask };
    bindings[HOTKEY_SUSPEND] = (HotkeyBinding){ XK_z, ControlMask };
}

static KeySym parse_keysym(const char *name) {
    if (strcasecmp(name, "up") == 0) return XK_Up;
    if (strcasecmp(name, "down") == 0) return XK_Down;
    if (strcasecmp(name, "left") == 0) return XK_Left;
    if (strcasecmp(name, "right") == 0) return XK_Right;
    if (strlen(name) == 1) {
        char c = name[0];
        if (c >= 'A' && c <= 'Z') c = c + ('a' - 'A');
        if (c >= 'a' && c <= 'z') return XK_a + (c - 'a');
        if (c >= '0' && c <= '9') return XK_0 + (c - '0');
    }
    return XStringToKeysym(name);
}

static int parse_key_spec(const char *spec, KeySym *out_sym, unsigned int *out_mod) {
    char copy[128];
    strncpy(copy, spec, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    unsigned int mod = 0;
    KeySym sym = NoSymbol;

    char *saveptr = NULL;
    char *token = strtok_r(copy, "+-", &saveptr);
    while (token) {
        while (isspace((unsigned char)*token)) token++;
        char *end = token + strlen(token) - 1;
        while (end > token && isspace((unsigned char)*end)) *end-- = '\0';

        if (strcasecmp(token, "ctrl") == 0 || strcasecmp(token, "control") == 0) {
            mod |= ControlMask;
        } else if (strcasecmp(token, "shift") == 0) {
            mod |= ShiftMask;
        } else if (strcasecmp(token, "alt") == 0 || strcasecmp(token, "mod1") == 0) {
            mod |= Mod1Mask;
        } else {
            sym = parse_keysym(token);
        }
        token = strtok_r(NULL, "+-", &saveptr);
    }

    if (sym == NoSymbol) {
        return 0;
    }
    *out_sym = sym;
    *out_mod = mod;
    return 1;
}

static int has_conflict(const HotkeyBinding bindings[HOTKEY_COUNT], int target_idx, KeySym sym, unsigned int mod) {
    for (int i = 0; i < HOTKEY_COUNT; i++) {
        if (i == target_idx) continue;
        KeySym s1 = bindings[i].keysym;
        KeySym s2 = sym;
        if (s1 >= 'A' && s1 <= 'Z') s1 += ('a' - 'A');
        if (s2 >= 'A' && s2 <= 'Z') s2 += ('a' - 'A');
        if (s1 == s2 && bindings[i].mod_mask == mod) {
            return 1;
        }
    }
    return 0;
}

void hotkeys_load_config(HotkeyBinding bindings[HOTKEY_COUNT]) {
    const char *home = getenv("HOME");
    if (!home) {
        return;
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/.terminatorrc", home);
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '#' || *p == '\0') continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';

        char *act_name = p;
        char *key_spec = eq + 1;

        char *end = act_name + strlen(act_name) - 1;
        while (end > act_name && isspace((unsigned char)*end)) *end-- = '\0';
        while (isspace((unsigned char)*key_spec)) key_spec++;
        end = key_spec + strlen(key_spec) - 1;
        while (end >= key_spec && (isspace((unsigned char)*end) || *end == '\n' || *end == '\r')) *end-- = '\0';

        int act_idx = -1;
        for (int i = 0; i < HOTKEY_COUNT; i++) {
            if (strcasecmp(act_name, action_names[i]) == 0) {
                act_idx = i;
                break;
            }
        }
        if (act_idx < 0) {
            fprintf(stderr, "terminator: unknown hotkey action '%s' in ~/.terminatorrc\n", act_name);
            continue;
        }

        KeySym sym;
        unsigned int mod;
        if (!parse_key_spec(key_spec, &sym, &mod)) {
            fprintf(stderr, "terminator: invalid key '%s' for action '%s' in ~/.terminatorrc\n", key_spec, act_name);
            continue;
        }

        if (has_conflict(bindings, act_idx, sym, mod)) {
            fprintf(stderr, "terminator: conflicting keybinding '%s' for action '%s' in ~/.terminatorrc\n", key_spec, act_name);
            continue;
        }

        bindings[act_idx].keysym = sym;
        bindings[act_idx].mod_mask = mod;
    }

    fclose(fp);
}

int hotkeys_match(const HotkeyBinding bindings[HOTKEY_COUNT], KeySym sym, unsigned int state) {
    unsigned int mask = state & (ControlMask | ShiftMask | Mod1Mask);
    for (int i = 0; i < HOTKEY_COUNT; i++) {
        KeySym s1 = bindings[i].keysym;
        KeySym s2 = sym;
        if (s1 >= 'A' && s1 <= 'Z') s1 += ('a' - 'A');
        if (s2 >= 'A' && s2 <= 'Z') s2 += ('a' - 'A');
        if (s1 == s2 && bindings[i].mod_mask == mask) {
            return i;
        }
    }
    return -1;
}
