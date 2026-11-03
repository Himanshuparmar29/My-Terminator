#ifndef TAB_H
#define TAB_H

#include "common.h"

typedef struct {
    int id;
    char cwd[1024];
    char **lines;
    int line_count;
    int line_capacity;
    char input_line[MAX_LINE_LEN];
    int input_len;
    int cursor_pos;
    int scroll_offset;
    int is_multiline;
    char multiline_accum[MAX_LINE_LEN * 4];
    int to_shell_fd;
    int from_shell_fd;
    pid_t shell_pid;
    pthread_t shell_thread;
    ExecutionMode mode;
    int is_alive;
    char escape_buf[1024];
    int escape_len;
    int in_escape;
    int history_browse_idx;
    int in_history_search;
} Tab;

Tab *tab_create(int id, const char *initial_cwd, ExecutionMode mode);
void tab_destroy(Tab *tab);
void tab_append_output(Tab *tab, const char *bytes, int count);
void tab_append_line(Tab *tab, const char *line);
void tab_insert_char(Tab *tab, char c);
void tab_backspace(Tab *tab);
void tab_cursor_left(Tab *tab);
void tab_cursor_right(Tab *tab);
void tab_scroll(Tab *tab, int delta);
void tab_clear_input(Tab *tab);
void tab_clear_screen(Tab *tab);

#endif
