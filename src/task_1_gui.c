#include "gui.h"
#include "parse.h"
#include "history.h"
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <ctype.h>

static XFontStruct *load_font(Display *dpy)
{
    const char *names[] = {"-*-fixed-bold-r-*-*-28-*-*-*-*-*-*-*", "-misc-fixed-bold-r-normal--24-*-*-*-*-*-*-*", "12x24", "10x20", "fixed", NULL};
    for (int i = 0; names[i]; i++)
    {
        XFontStruct *fs = XLoadQueryFont(dpy, names[i]);
        if (fs)
            return fs;
    }
    return NULL;
}

static const char *get_disp_cwd(const char *cwd, char *buf, size_t sz)
{
    const char *h = getenv("HOME");
    if (h && strncmp(cwd, h, strlen(h)) == 0)
    {
        snprintf(buf, sz, "~%s", cwd + strlen(h));
        return buf;
    }
    return cwd;
}

int gui_add_tab(GuiApp *app, const char *initial_cwd)
{
    if (!app || app->tab_count >= MAX_TABS)
        return -1;
    Tab *tab = tab_create(app->next_tab_id++, initial_cwd, global_exec_mode);
    if (!tab)
        return -1;
    app->tabs[app->tab_count] = tab;
    return app->active_tab_index = app->tab_count++;
}

void gui_switch_tab(GuiApp *app, int index)
{
    if (app && index >= 0 && index < app->tab_count)
        app->active_tab_index = index;
}

void gui_close_tab(GuiApp *app, int index)
{
    if (!app || index < 0 || index >= app->tab_count)
        return;
    tab_destroy(app->tabs[index]);
    for (int i = index; i < app->tab_count - 1; i++)
        app->tabs[i] = app->tabs[i + 1];
    if (--app->tab_count == 0)
        app->running = 0;
    else if (app->active_tab_index >= app->tab_count)
        app->active_tab_index = app->tab_count - 1;
}

GuiApp *gui_init(ExecutionMode mode)
{
    (void)mode;
    GuiApp *app = calloc(1, sizeof(GuiApp));
    if (!app)
        return NULL;
    const char *disp = getenv("DISPLAY");
    app->display = XOpenDisplay(disp ? disp : ":0");
    if (!app->display)
        app->display = XOpenDisplay(":0");
    if (!app->display)
    {
        fprintf(stderr, "terminator: cannot open X display\n");
        free(app);
        return NULL;
    }
    app->screen = DefaultScreen(app->display);
    app->win_width = 1050;
    app->win_height = 680;
    app->bg_pixel = BlackPixel(app->display, app->screen);
    app->fg_pixel = WhitePixel(app->display, app->screen);
    app->font = load_font(app->display);
    app->font_width = (app->font && app->font->max_bounds.width > 0) ? app->font->max_bounds.width : 8;
    app->font_ascent = app->font ? app->font->ascent : 12;
    app->font_descent = app->font ? app->font->descent : 4;
    app->line_height = app->font_ascent + app->font_descent + 4;
    app->tab_bar_height = app->line_height + 10;
    app->window = XCreateSimpleWindow(app->display, RootWindow(app->display, app->screen), 0, 0, app->win_width, app->win_height, 1, app->fg_pixel, app->bg_pixel);
    XStoreName(app->display, app->window, "Terminator");
    XSelectInput(app->display, app->window, KeyPressMask | ButtonPressMask | ButtonReleaseMask | Button1MotionMask | ExposureMask | StructureNotifyMask | FocusChangeMask);
    app->gc = XCreateGC(app->display, app->window, 0, NULL);
    if (app->font)
        XSetFont(app->display, app->gc, app->font->fid);
    XMapWindow(app->display, app->window);
    XFlush(app->display);
    app->next_tab_id = 1;
    app->running = 1;
    hotkeys_init(app->hotkeys);
    hotkeys_load_config(app->hotkeys);
    gui_add_tab(app, NULL);
    return app;
}

static void draw_tab_bar(GuiApp *app)
{
    XSetForeground(app->display, app->gc, app->bg_pixel);
    XFillRectangle(app->display, app->window, app->gc, 0, 0, app->win_width, app->tab_bar_height);
    XSetForeground(app->display, app->gc, app->fg_pixel);
    XDrawLine(app->display, app->window, app->gc, 0, app->tab_bar_height, app->win_width, app->tab_bar_height);
    int plus_w = (app->tab_count < MAX_TABS) ? (app->font_width * 7 + 8) : 0;
    int n = app->tab_count > 0 ? app->tab_count : 1;
    int max_w = (app->win_width - 10 - plus_w - 6 * n) / n;
    if (max_w < app->font_width * 6)
        max_w = app->font_width * 6;
    int cur_x = 5, ty = 2 + (app->tab_bar_height - 3 + app->font_ascent - app->font_descent) / 2;
    for (int i = 0; i < app->tab_count; i++)
    {
        char buf[512], label[512];
        snprintf(label, sizeof(label), " [ %d: %s x ] ", app->tabs[i]->id, get_disp_cwd(app->tabs[i]->cwd, buf, sizeof(buf)));
        int w = strlen(label) * app->font_width + 8;
        if (w > max_w)
            w = max_w;
        XSetForeground(app->display, app->gc, app->fg_pixel);
        if (i == app->active_tab_index)
        {
            XFillRectangle(app->display, app->window, app->gc, cur_x, 2, w, app->tab_bar_height - 3);
            XSetForeground(app->display, app->gc, app->bg_pixel);
        }
        else
            XDrawRectangle(app->display, app->window, app->gc, cur_x, 2, w, app->tab_bar_height - 3);
        int fit = (w - 8) / app->font_width;
        if (fit > (int)strlen(label))
            fit = strlen(label);
        if (fit > 0)
            XDrawString(app->display, app->window, app->gc, cur_x + 4, ty, label, fit);
        cur_x += w + 6;
    }
    if (app->tab_count < MAX_TABS)
    {
        XSetForeground(app->display, app->gc, app->fg_pixel);
        XDrawRectangle(app->display, app->window, app->gc, cur_x, 2, plus_w - 6, app->tab_bar_height - 3);
        XDrawString(app->display, app->window, app->gc, cur_x + 4, ty, " [ + ] ", 7);
    }
}

static void draw_terminal_content(GuiApp *app)
{
    if (app->active_tab_index < 0 || app->active_tab_index >= app->tab_count)
        return;
    Tab *tab = app->tabs[app->active_tab_index];
    int top = app->tab_bar_height + 2, rows = (app->win_height - top) / app->line_height;
    int cols = (app->win_width - 16) / app->font_width;
    if (rows <= 0 || cols <= 0)
        return;
    XRectangle clip_r = {0, (short)top, (unsigned short)app->win_width, (unsigned short)(app->win_height - top)};
    XSetClipRectangles(app->display, app->gc, 0, 0, &clip_r, 1, Unsorted);
    XSetForeground(app->display, app->gc, app->bg_pixel);
    XFillRectangle(app->display, app->window, app->gc, 0, top, app->win_width, app->win_height - top);
    int total_vrows = 0;
    for (int i = 0; i < tab->line_count; i++)
    {
        int flen = strlen(tab->lines[i]) + ((i == tab->line_count - 1) ? tab->input_len : 0);
        total_vrows += (flen > 0) ? (flen + cols - 1) / cols : 1;
    }
    int start_vrow = total_vrows - rows - tab->scroll_offset;
    if (start_vrow < 0)
        start_vrow = 0;
    int sx1 = app->sel_start_x, sy1 = app->sel_start_y, sx2 = app->sel_end_x, sy2 = app->sel_end_y;
    if (sy1 > sy2 || (sy1 == sy2 && sx1 > sx2))
    {
        int tx = sx1;
        sx1 = sx2;
        sx2 = tx;
        int ty = sy1;
        sy1 = sy2;
        sy2 = ty;
    }
    int vrow_idx = 0, y = top + app->font_ascent + 2;
    for (int i = 0; i < tab->line_count && (y - app->font_ascent) < app->win_height; i++)
    {
        if (!tab->lines[i])
            continue;
        char full[MAX_LINE_LEN * 2];
        snprintf(full, sizeof(full), "%s%s", tab->lines[i], (i == tab->line_count - 1) ? tab->input_line : "");
        int flen = strlen(full), vrows = (flen > 0) ? (flen + cols - 1) / cols : 1;
        for (int r = 0; r < vrows && (y - app->font_ascent) < app->win_height; r++, vrow_idx++)
        {
            if (vrow_idx < start_vrow)
                continue;
            int offset = r * cols, clen = flen - offset;
            if (clen > cols)
                clen = cols;
            int row_top = y - app->font_ascent, row_bot = row_top + app->line_height;
            if (clen > 0)
            {
                XSetForeground(app->display, app->gc, app->fg_pixel);
                XDrawString(app->display, app->window, app->gc, 8, y, full + offset, clen);
                if (app->sel_active && row_bot > sy1 && row_top < sy2)
                {
                    int c_start = (sy1 >= row_top && sy1 < row_bot) ? (sx1 - 8) / app->font_width : 0;
                    int c_end = (sy2 >= row_top && sy2 < row_bot) ? (sx2 - 8 + app->font_width - 1) / app->font_width : clen;
                    if (c_start < 0)
                        c_start = 0;
                    if (c_end > clen)
                        c_end = clen;
                    if (c_start < c_end)
                    {
                        XSetForeground(app->display, app->gc, app->fg_pixel);
                        XFillRectangle(app->display, app->window, app->gc, 8 + c_start * app->font_width, row_top, (c_end - c_start) * app->font_width, app->line_height);
                        XSetForeground(app->display, app->gc, app->bg_pixel);
                        XDrawString(app->display, app->window, app->gc, 8 + c_start * app->font_width, y, full + offset + c_start, c_end - c_start);
                    }
                }
            }
            if (i == tab->line_count - 1 && ((int)strlen(tab->lines[i]) + tab->cursor_pos) / cols == r)
            {
                XSetForeground(app->display, app->gc, app->fg_pixel);
                XFillRectangle(app->display, app->window, app->gc, 8 + (((int)strlen(tab->lines[i]) + tab->cursor_pos) % cols) * app->font_width, y - app->font_ascent, app->font_width, app->line_height - 2);
            }
            y += app->line_height;
        }
    }
    XSetClipMask(app->display, app->gc, None);
}

static void gui_redraw(GuiApp *app)
{
    draw_tab_bar(app);
    draw_terminal_content(app);
    XFlush(app->display);
}

static void gui_paste(GuiApp *app, Tab *tab)
{
    if (!app || !tab)
        return;
    Atom clip = XInternAtom(app->display, "CLIPBOARD", False);
    Atom utf8 = XInternAtom(app->display, "UTF8_STRING", False);
    Atom prop = XInternAtom(app->display, "TERMINATOR_SELECTION", False);
    Atom sel = clip;
    Window owner = XGetSelectionOwner(app->display, clip);
    if (owner == None)
    {
        owner = XGetSelectionOwner(app->display, XA_PRIMARY);
        if (owner != None)
            sel = XA_PRIMARY;
    }
    if (owner == app->window)
    {
        for (size_t k = 0; k < strlen(app->clipboard); k++)
        {
            if (app->clipboard[k] != '\r')
                tab_insert_char(tab, app->clipboard[k]);
        }
        return;
    }
    if (owner != None)
    {
        Atom target_types[] = {utf8, XA_STRING};
        for (int t = 0; t < 2; t++)
        {
            XConvertSelection(app->display, sel, target_types[t], prop, app->window, CurrentTime);
            XFlush(app->display);
            XEvent ev;
            int got = 0;
            for (int i = 0; i < 20; i++)
            {
                if (XCheckTypedWindowEvent(app->display, app->window, SelectionNotify, &ev))
                {
                    got = 1;
                    break;
                }
                struct pollfd pfd = {.fd = ConnectionNumber(app->display), .events = POLLIN, .revents = 0};
                poll(&pfd, 1, 10);
            }
            if (got && ev.xselection.property != None)
            {
                Atom type;
                int format;
                unsigned long nitems = 0, bytes_after = 0;
                unsigned char *data = NULL;
                if (XGetWindowProperty(app->display, app->window, prop, 0, 65536, True, AnyPropertyType, &type, &format, &nitems, &bytes_after, &data) == Success && data)
                {
                    for (unsigned long k = 0; k < nitems; k++)
                    {
                        if (data[k] != '\r')
                            tab_insert_char(tab, (char)data[k]);
                    }
                    snprintf(app->clipboard, sizeof(app->clipboard), "%.*s", (int)(nitems < sizeof(app->clipboard) - 1 ? nitems : sizeof(app->clipboard) - 1), data);
                    XFree(data);
                    return;
                }
            }
        }
    }
    for (size_t k = 0; k < strlen(app->clipboard); k++)
    {
        if (app->clipboard[k] != '\r')
            tab_insert_char(tab, app->clipboard[k]);
    }
}

static void gui_extract_selection(GuiApp *app)
{
    if (!app || app->active_tab_index < 0 || app->active_tab_index >= app->tab_count)
        return;
    Tab *tab = app->tabs[app->active_tab_index];
    int top = app->tab_bar_height + 2, rows = (app->win_height - top) / app->line_height;
    int cols = (app->win_width - 16) / app->font_width;
    if (rows <= 0 || cols <= 0)
        return;

    int sx1 = app->sel_start_x, sy1 = app->sel_start_y;
    int sx2 = app->sel_end_x, sy2 = app->sel_end_y;
    if (abs(sx1 - sx2) < 3 && abs(sy1 - sy2) < 3)
    {
        app->sel_active = 0;
        return;
    }
    if (sy1 > sy2 || (sy1 == sy2 && sx1 > sx2))
    {
        int tx = sx1;
        sx1 = sx2;
        sx2 = tx;
        int ty = sy1;
        sy1 = sy2;
        sy2 = ty;
    }

    int total_vrows = 0;
    for (int i = 0; i < tab->line_count; i++)
    {
        int flen = strlen(tab->lines[i]) + ((i == tab->line_count - 1) ? tab->input_len : 0);
        total_vrows += (flen > 0) ? (flen + cols - 1) / cols : 1;
    }
    int start_vrow = total_vrows - rows - tab->scroll_offset;
    if (start_vrow < 0)
        start_vrow = 0;

    int vrow_idx = 0, y = top + app->font_ascent + 2;
    int out_pos = 0;
    app->clipboard[0] = '\0';

    for (int i = 0; i < tab->line_count && (y - app->font_ascent) < app->win_height; i++)
    {
        if (!tab->lines[i])
            continue;
        char full[MAX_LINE_LEN * 2];
        snprintf(full, sizeof(full), "%s%s", tab->lines[i], (i == tab->line_count - 1) ? tab->input_line : "");
        int flen = strlen(full), vrows = (flen > 0) ? (flen + cols - 1) / cols : 1;
        for (int r = 0; r < vrows && (y - app->font_ascent) < app->win_height; r++, vrow_idx++)
        {
            if (vrow_idx >= start_vrow)
            {
                int row_top = y - app->font_ascent;
                int row_bot = row_top + app->line_height;
                if (row_bot > sy1 && row_top < sy2)
                {
                    int offset = r * cols, clen = flen - offset;
                    if (clen > cols)
                        clen = cols;
                    int c_start = (sy1 >= row_top && sy1 < row_bot) ? (sx1 - 8) / app->font_width : 0;
                    int c_end = (sy2 >= row_top && sy2 < row_bot) ? (sx2 - 8 + app->font_width - 1) / app->font_width : clen;
                    if (c_start < 0)
                        c_start = 0;
                    if (c_end > clen)
                        c_end = clen;
                    if (c_start < c_end && clen > 0)
                    {
                        for (int k = c_start; k < c_end && out_pos + 1 < (int)sizeof(app->clipboard); k++)
                        {
                            app->clipboard[out_pos++] = full[offset + k];
                        }
                        if (row_bot <= sy2 && out_pos + 1 < (int)sizeof(app->clipboard))
                        {
                            app->clipboard[out_pos++] = '\n';
                        }
                    }
                }
                y += app->line_height;
            }
        }
    }
    app->clipboard[out_pos] = '\0';
    if (out_pos > 0)
    {
        XSetSelectionOwner(app->display, XA_PRIMARY, app->window, CurrentTime);
        XSetSelectionOwner(app->display, XInternAtom(app->display, "CLIPBOARD", False), app->window, CurrentTime);
        XFlush(app->display);
    }
    else
    {
        app->sel_active = 0;
    }
}

static void handle_click(GuiApp *app, int x, int y, unsigned int button)
{
    if ((button == Button4 || button == Button5) && app->active_tab_index >= 0 && app->active_tab_index < app->tab_count)
    {
        tab_scroll(app->tabs[app->active_tab_index], (button == Button4) ? 3 : -3);
        gui_redraw(app);
        return;
    }
    if (y > app->tab_bar_height)
    {
        if (button == Button2 && app->active_tab_index >= 0 && app->active_tab_index < app->tab_count)
        {
            gui_paste(app, app->tabs[app->active_tab_index]);
            gui_redraw(app);
        }
        else if (button == Button1)
        {
            app->sel_active = 1;
            app->sel_start_x = app->sel_end_x = x;
            app->sel_start_y = app->sel_end_y = y;
            gui_redraw(app);
        }
        return;
    }
    int plus_w = (app->tab_count < MAX_TABS) ? (app->font_width * 7 + 8) : 0;
    int n = app->tab_count > 0 ? app->tab_count : 1;
    int max_w = (app->win_width - 10 - plus_w - 6 * n) / n;
    if (max_w < app->font_width * 6)
        max_w = app->font_width * 6;
    int cur_x = 5;
    for (int i = 0; i < app->tab_count; i++)
    {
        char buf[512], label[512];
        snprintf(label, sizeof(label), " [ %d: %s x ] ", app->tabs[i]->id, get_disp_cwd(app->tabs[i]->cwd, buf, sizeof(buf)));
        int w = strlen(label) * app->font_width + 8;
        if (w > max_w)
            w = max_w;
        if (x >= cur_x && x < cur_x + w)
        {
            if (x >= cur_x + w - app->font_width * 3 || button == Button2)
                gui_close_tab(app, i);
            else
                gui_switch_tab(app, i);
            gui_redraw(app);
            return;
        }
        cur_x += w + 6;
    }
    if (app->tab_count < MAX_TABS && x >= cur_x && x < cur_x + plus_w)
    {
        gui_add_tab(app, (app->active_tab_index >= 0 && app->tabs[app->active_tab_index]) ? app->tabs[app->active_tab_index]->cwd : NULL);
        gui_redraw(app);
    }
}

static void handle_enter(GuiApp *app, Tab *tab)
{
    if (tab->in_history_search)
    {
        tab->in_history_search = 0;
        char term[MAX_LINE_LEN];
        snprintf(term, sizeof(term), "%s", tab->input_line);
        if (tab->line_count > 0 && strlen(tab->lines[tab->line_count - 1]) + tab->input_len < MAX_LINE_LEN)
        {
            strcat(tab->lines[tab->line_count - 1], tab->input_line);
        }
        char res[64][MAX_LINE_LEN];
        int n = history_search(term, res, 64);
        if (n > 0)
        {
            for (int k = 0; k < n; k++)
                tab_append_line(tab, res[k]);
        }
        else
            tab_append_line(tab, "No match for search term in history");
        char pb[512], pr[512];
        snprintf(pr, sizeof(pr), "user@terminator:%s> ", get_disp_cwd(tab->cwd, pb, sizeof(pb)));
        tab_append_line(tab, pr);
        tab_clear_input(tab);
        gui_redraw(app);
        return;
    }
    size_t len = tab->is_multiline ? strlen(tab->multiline_accum) : 0;
    if (len + tab->input_len < sizeof(tab->multiline_accum))
    {
        memcpy(tab->multiline_accum + len, tab->input_line, tab->input_len);
        tab->multiline_accum[len + tab->input_len] = '\0';
    }
    if (tab->line_count > 0 && strlen(tab->lines[tab->line_count - 1]) + tab->input_len < MAX_LINE_LEN)
    {
        strcat(tab->lines[tab->line_count - 1], tab->input_line);
    }
    int in_q = 0;
    if (parse_is_incomplete_state(tab->multiline_accum, &in_q))
    {
        tab->is_multiline = 1;
        size_t alen = strlen(tab->multiline_accum);
        if (alen > 0 && tab->multiline_accum[alen - 1] == '\\')
            tab->multiline_accum[alen - 1] = in_q ? '\n' : ' ';
        else
            strncat(tab->multiline_accum, "\n", sizeof(tab->multiline_accum) - alen - 1);
        tab_append_line(tab, "> ");
    }
    else
    {
        tab->is_multiline = 0;
        dprintf(tab->to_shell_fd, "%s\n", tab->multiline_accum);
        tab->multiline_accum[0] = '\0';
        tab_append_line(tab, "");
    }
    tab_clear_input(tab);
    gui_redraw(app);
}

static void handle_key(GuiApp *app, XKeyEvent *ev)
{
    if (app->active_tab_index < 0 || app->active_tab_index >= app->tab_count)
        return;
    Tab *tab = app->tabs[app->active_tab_index];
    char str[64];
    KeySym keysym;
    int len = XLookupString(ev, str, sizeof(str) - 1, &keysym, NULL);
    str[len] = '\0';

    if (keysym == XK_Escape)
    {
        app->sel_active = 0;
        gui_redraw(app);
        return;
    }

    int is_ctrl = (ev->state & ControlMask) != 0, is_shift = (ev->state & ShiftMask) != 0, is_alt = (ev->state & Mod1Mask) != 0;
    if (is_ctrl && is_shift && (keysym == XK_t || keysym == XK_T))
    {
        gui_add_tab(app, tab->cwd);
        gui_redraw(app);
        return;
    }
    if (is_ctrl && is_shift && (keysym == XK_w || keysym == XK_W))
    {
        gui_close_tab(app, app->active_tab_index);
        gui_redraw(app);
        return;
    }
    if (is_ctrl && (keysym == XK_r || keysym == XK_R))
    {
        if (!tab->in_history_search)
        {
            tab->in_history_search = 1;
            tab_clear_input(tab);
            tab_append_line(tab, "Enter search term: ");
            gui_redraw(app);
        }
        return;
    }
    if (is_alt && keysym >= XK_1 && keysym <= XK_9 && (int)(keysym - XK_1) < app->tab_count)
    {
        gui_switch_tab(app, keysym - XK_1);
        gui_redraw(app);
        return;
    }

    int act = hotkeys_match(app->hotkeys, keysym, ev->state);
    if (act == HOTKEY_MOVE_START)
        tab->cursor_pos = 0;
    else if (act == HOTKEY_MOVE_END)
        tab->cursor_pos = tab->input_len;
    else if (act == HOTKEY_HISTORY_PREV || act == HOTKEY_HISTORY_NEXT)
    {
        int total = history_get_count();
        if (total > 0)
        {
            tab->history_browse_idx = (act == HOTKEY_HISTORY_PREV) ? ((tab->history_browse_idx == -1) ? total - 1 : (tab->history_browse_idx > 0 ? tab->history_browse_idx - 1 : 0)) : (tab->history_browse_idx != -1 ? tab->history_browse_idx + 1 : -1);
            char ent[MAX_LINE_LEN];
            if (tab->history_browse_idx >= 0 && tab->history_browse_idx < total && history_get_entry(tab->history_browse_idx, ent, sizeof(ent)))
            {
                snprintf(tab->input_line, sizeof(tab->input_line), "%s", ent);
                tab->input_len = tab->cursor_pos = strlen(tab->input_line);
            }
            else if (tab->history_browse_idx >= total)
                tab_clear_input(tab);
        }
    }
    else if (act == HOTKEY_DEL_TO_START && tab->cursor_pos > 0)
    {
        memmove(tab->input_line, tab->input_line + tab->cursor_pos, tab->input_len - tab->cursor_pos + 1);
        tab->input_len -= tab->cursor_pos;
        tab->cursor_pos = 0;
    }
    else if (act == HOTKEY_DEL_TO_END)
    {
        tab->input_line[tab->cursor_pos] = '\0';
        tab->input_len = tab->cursor_pos;
    }
    else if (act == HOTKEY_DEL_PREV_WORD && tab->cursor_pos > 0)
    {
        int pos = tab->cursor_pos;
        while (pos > 0 && isspace((unsigned char)tab->input_line[pos - 1]))
            pos--;
        while (pos > 0 && !isspace((unsigned char)tab->input_line[pos - 1]))
            pos--;
        memmove(tab->input_line + pos, tab->input_line + tab->cursor_pos, tab->input_len - tab->cursor_pos + 1);
        tab->input_len -= (tab->cursor_pos - pos);
        tab->cursor_pos = pos;
    }
    else if (act == HOTKEY_CLEAR_SCREEN)
    {
        char pcopy[MAX_LINE_LEN] = "";
        if (tab->line_count > 0 && tab->lines[tab->line_count - 1])
            snprintf(pcopy, sizeof(pcopy), "%s", tab->lines[tab->line_count - 1]);
        tab_clear_screen(tab);
        snprintf(tab->lines[0], MAX_LINE_LEN, "%s", pcopy);
    }
    else if (act == HOTKEY_EXIT_SHELL)
    {
        if (tab->input_len == 0)
            gui_close_tab(app, app->active_tab_index);
        else if (tab->cursor_pos < tab->input_len)
        {
            memmove(tab->input_line + tab->cursor_pos, tab->input_line + tab->cursor_pos + 1, tab->input_len - tab->cursor_pos);
            tab->input_len--;
        }
    }
    else if (act == HOTKEY_COPY)
    {
        if (!app->sel_active || strlen(app->clipboard) == 0)
        {
            if (tab->input_len > 0)
            {
                snprintf(app->clipboard, sizeof(app->clipboard), "%s", tab->input_line);
            }
            else if (tab->line_count > 1 && tab->lines[tab->line_count - 2])
            {
                snprintf(app->clipboard, sizeof(app->clipboard), "%s", tab->lines[tab->line_count - 2]);
            }
        }
        XSetSelectionOwner(app->display, XA_PRIMARY, app->window, CurrentTime);
        XSetSelectionOwner(app->display, XInternAtom(app->display, "CLIPBOARD", False), app->window, CurrentTime);
        XFlush(app->display);
        return;
    }
    else if (act == HOTKEY_PASTE)
    {
        gui_paste(app, tab);
    }
    else if (act == HOTKEY_SUSPEND || (is_ctrl && !is_shift && (keysym == XK_c || keysym == XK_C)))
    {
        if (app->sel_active && strlen(app->clipboard) > 0)
        {
            XSetSelectionOwner(app->display, XA_PRIMARY, app->window, CurrentTime);
            XSetSelectionOwner(app->display, XInternAtom(app->display, "CLIPBOARD", False), app->window, CurrentTime);
            XFlush(app->display);
            return;
        }
        if (tab->in_history_search)
        {
            tab->in_history_search = 0;
            tab_clear_input(tab);
            char pb[512], pr[512];
            snprintf(pr, sizeof(pr), "user@terminator:%s> ", get_disp_cwd(tab->cwd, pb, sizeof(pb)));
            tab_append_line(tab, pr);
            gui_redraw(app);
            return;
        }
        dprintf(tab->to_shell_fd, (act == HOTKEY_SUSPEND) ? "\x1a\n" : "\x03\n");
        tab_clear_input(tab);
        tab->is_multiline = 0;
        tab->multiline_accum[0] = '\0';
    }
    else if (keysym == XK_Return || keysym == XK_KP_Enter)
    {
        handle_enter(app, tab);
        return;
    }
    else if (keysym == XK_BackSpace)
        tab_backspace(tab);
    else if (keysym == XK_Left)
        tab_cursor_left(tab);
    else if (keysym == XK_Right)
        tab_cursor_right(tab);
    else if (keysym == XK_Page_Up || keysym == XK_Prior)
        tab_scroll(tab, 5);
    else if (keysym == XK_Page_Down || keysym == XK_Next)
        tab_scroll(tab, -5);
    else if (len > 0 && (unsigned char)str[0] >= 32)
        tab_insert_char(tab, str[0]);
    else
        return;

    gui_redraw(app);
}

static void handle_selection_request(GuiApp *app, XSelectionRequestEvent *req)
{
    XEvent resp;
    memset(&resp, 0, sizeof(resp));
    resp.xselection.type = SelectionNotify;
    resp.xselection.display = req->display;
    resp.xselection.requestor = req->requestor;
    resp.xselection.selection = req->selection;
    resp.xselection.target = req->target;
    resp.xselection.time = req->time;
    Atom prop = (req->property == None) ? req->target : req->property;
    resp.xselection.property = prop;

    Atom utf8 = XInternAtom(app->display, "UTF8_STRING", False);
    Atom targets = XInternAtom(app->display, "TARGETS", False);
    Atom text = XInternAtom(app->display, "TEXT", False);
    Atom plain = XInternAtom(app->display, "text/plain", False);
    Atom plain_utf8 = XInternAtom(app->display, "text/plain;charset=utf-8", False);

    if (req->target == targets)
    {
        Atom supp[] = {targets, utf8, XA_STRING, text, plain, plain_utf8};
        XChangeProperty(req->display, req->requestor, prop, XA_ATOM, 32, PropModeReplace, (unsigned char *)supp, 6);
    }
    else if (req->target == XA_STRING || req->target == utf8 || req->target == text || req->target == plain || req->target == plain_utf8)
    {
        Atom ptype = (req->target == text || req->target == plain) ? XA_STRING : req->target;
        XChangeProperty(req->display, req->requestor, prop, ptype, 8, PropModeReplace, (unsigned char *)app->clipboard, strlen(app->clipboard));
    }
    else
    {
        resp.xselection.property = None;
    }
    XSendEvent(req->display, req->requestor, False, 0, &resp);
    XFlush(app->display);
}

void gui_run(GuiApp *app)
{
    if (!app)
        return;
    gui_redraw(app);
    while (app->running && app->tab_count > 0)
    {
        while (XPending(app->display))
        {
            XEvent ev;
            XNextEvent(app->display, &ev);
            if (ev.type == Expose)
                gui_redraw(app);
            else if (ev.type == ConfigureNotify)
            {
                app->win_width = ev.xconfigure.width;
                app->win_height = ev.xconfigure.height;
                gui_redraw(app);
            }
            else if (ev.type == KeyPress)
                handle_key(app, &ev.xkey);
            else if (ev.type == ButtonPress)
                handle_click(app, ev.xbutton.x, ev.xbutton.y, ev.xbutton.button);
            else if (ev.type == ButtonRelease)
            {
                if (ev.xbutton.button == Button1 && app->sel_active)
                {
                    app->sel_end_x = ev.xbutton.x;
                    app->sel_end_y = ev.xbutton.y;
                    gui_extract_selection(app);
                    gui_redraw(app);
                }
            }
            else if (ev.type == MotionNotify)
            {
                if (app->sel_active)
                {
                    app->sel_end_x = ev.xmotion.x;
                    app->sel_end_y = ev.xmotion.y;
                    gui_redraw(app);
                }
            }
            else if (ev.type == SelectionRequest)
                handle_selection_request(app, &ev.xselectionrequest);
        }
        struct pollfd pfds[MAX_TABS + 1];
        int tmap[MAX_TABS + 1], nfds = 1;
        pfds[0] = (struct pollfd){.fd = ConnectionNumber(app->display), .events = POLLIN, .revents = 0};
        for (int i = 0; i < app->tab_count; i++)
        {
            if (app->tabs[i] && app->tabs[i]->from_shell_fd >= 0)
            {
                pfds[nfds] = (struct pollfd){.fd = app->tabs[i]->from_shell_fd, .events = POLLIN, .revents = 0};
                tmap[nfds++] = i;
            }
        }
        XFlush(app->display);
        if (poll(pfds, nfds, 50) <= 0)
            continue;
        int redraw = 0;
        for (int k = 1; k < nfds; k++)
        {
            if (pfds[k].revents & (POLLIN | POLLHUP | POLLERR))
            {
                Tab *tab = app->tabs[tmap[k]];
                char buf[4096];
                int n = read(tab->from_shell_fd, buf, sizeof(buf));
                if (n > 0)
                {
                    tab_append_output(tab, buf, n);
                    redraw = 1;
                }
                else if (n <= 0)
                {
                    if (n < 0 && (errno == EAGAIN || errno == EINTR))
                        continue;
                    gui_close_tab(app, tmap[k]);
                    redraw = 1;
                    break;
                }
            }
        }
        if (redraw)
            gui_redraw(app);
    }
}

void gui_cleanup(GuiApp *app)
{
    if (!app)
        return;
    while (app->tab_count > 0)
        gui_close_tab(app, 0);
    if (app->gc) XFreeGC(app->display, app->gc);
    if (app->font)
        XFreeFont(app->display, app->font);
    if (app->window)
        XDestroyWindow(app->display, app->window);
    if (app->display)
        XCloseDisplay(app->display);
    free(app);
}
