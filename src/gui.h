#ifndef GUI_H
#define GUI_H

#include "common.h"
#include "tab.h"
#include "hotkeys.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>

typedef struct {
    Display *display;
    int screen;
    Window window;
    GC gc;
    XFontStruct *font;
    int font_width;
    int font_ascent;
    int font_descent;
    int line_height;
    int win_width;
    int win_height;
    int tab_bar_height;
    unsigned long bg_pixel;
    unsigned long fg_pixel;
    Tab *tabs[MAX_TABS];
    int tab_count;
    int active_tab_index;
    int next_tab_id;
    int running;
    HotkeyBinding hotkeys[HOTKEY_COUNT];
    char clipboard[MAX_LINE_LEN];
    int sel_start_x;
    int sel_start_y;
    int sel_end_x;
    int sel_end_y;
    int sel_active;
} GuiApp;

GuiApp *gui_init(ExecutionMode mode);
void gui_run(GuiApp *app);
void gui_cleanup(GuiApp *app);
int gui_add_tab(GuiApp *app, const char *initial_cwd);
void gui_close_tab(GuiApp *app, int index);
void gui_switch_tab(GuiApp *app, int index);

#endif
