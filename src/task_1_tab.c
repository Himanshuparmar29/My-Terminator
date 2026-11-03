#include "tab.h"
#include "shell.h"

Tab *tab_create(int id, const char *initial_cwd, ExecutionMode mode) {
    Tab *tab = malloc(sizeof(Tab));
    if (!tab) {
        return NULL;
    }
    memset(tab, 0, sizeof(Tab));

    tab->id = id;
    tab->mode = mode;
    tab->is_alive = 1;
    tab->history_browse_idx = -1;

    if (initial_cwd && strlen(initial_cwd) > 0) {
        strncpy(tab->cwd, initial_cwd, sizeof(tab->cwd) - 1);
        tab->cwd[sizeof(tab->cwd) - 1] = '\0';
    } else {
        const char *home = getenv("HOME");
        if (home) {
            strncpy(tab->cwd, home, sizeof(tab->cwd) - 1);
            tab->cwd[sizeof(tab->cwd) - 1] = '\0';
        } else if (!getcwd(tab->cwd, sizeof(tab->cwd))) {
            strcpy(tab->cwd, "/");
        }
    }

    tab->line_capacity = 256;
    tab->lines = malloc(sizeof(char *) * tab->line_capacity);
    tab->line_count = 0;
    tab->lines[0] = calloc(MAX_LINE_LEN, 1);
    tab->line_count = 1;

    int gui_to_shell[2];
    int shell_to_gui[2];
    if (pipe(gui_to_shell) < 0 || pipe(shell_to_gui) < 0) {
        free(tab->lines[0]);
        free(tab->lines);
        free(tab);
        return NULL;
    }

    tab->to_shell_fd = gui_to_shell[1];
    tab->from_shell_fd = shell_to_gui[0];

    if (mode == MODE_THREAD) {
        ShellThreadArgs *args = malloc(sizeof(ShellThreadArgs));
        args->in_fd = gui_to_shell[0];
        args->out_fd = shell_to_gui[1];
        strncpy(args->initial_cwd, tab->cwd, sizeof(args->initial_cwd) - 1);
        args->initial_cwd[sizeof(args->initial_cwd) - 1] = '\0';
        args->tab_id = id;

        if (pthread_create(&tab->shell_thread, NULL, shell_thread_entry, args) != 0) {
            free(args);
            close(gui_to_shell[0]);
            close(gui_to_shell[1]);
            close(shell_to_gui[0]);
            close(shell_to_gui[1]);
            free(tab->lines[0]);
            free(tab->lines);
            free(tab);
            return NULL;
        }
    } else {
        pid_t pid = fork();
        if (pid < 0) {
            close(gui_to_shell[0]);
            close(gui_to_shell[1]);
            close(shell_to_gui[0]);
            close(shell_to_gui[1]);
            free(tab->lines[0]);
            free(tab->lines);
            free(tab);
            return NULL;
        }
        if (pid == 0) {
            close(gui_to_shell[1]);
            close(shell_to_gui[0]);
            shell_process_entry(gui_to_shell[0], shell_to_gui[1], tab->cwd);
            _exit(0);
        }
        close(gui_to_shell[0]);
        close(shell_to_gui[1]);
        tab->shell_pid = pid;
    }

    return tab;
}

void tab_destroy(Tab *tab) {
    if (!tab) {
        return;
    }

    if (tab->to_shell_fd >= 0) {
        close(tab->to_shell_fd);
        tab->to_shell_fd = -1;
    }
    if (tab->from_shell_fd >= 0) {
        close(tab->from_shell_fd);
        tab->from_shell_fd = -1;
    }

    if (tab->mode == MODE_PROCESS && tab->shell_pid > 0) {
        kill(tab->shell_pid, SIGTERM);
        usleep(10000);
        kill(tab->shell_pid, SIGKILL);
        waitpid(tab->shell_pid, NULL, WNOHANG);
    } else if (tab->mode == MODE_THREAD && tab->is_alive) {
        pthread_cancel(tab->shell_thread);
        pthread_join(tab->shell_thread, NULL);
    }

    for (int i = 0; i < tab->line_count; i++) {
        if (tab->lines[i]) {
            free(tab->lines[i]);
        }
    }
    free(tab->lines);
    free(tab);
}

void tab_append_line(Tab *tab, const char *line) {
    if (!tab || !line) {
        return;
    }

    if (tab->line_count >= MAX_OUTPUT_LINES) {
        free(tab->lines[0]);
        for (int i = 1; i < tab->line_count; i++) {
            tab->lines[i - 1] = tab->lines[i];
        }
        tab->line_count--;
    } else if (tab->line_count >= tab->line_capacity) {
        tab->line_capacity *= 2;
        tab->lines = realloc(tab->lines, sizeof(char *) * tab->line_capacity);
    }

    char *new_line = malloc(MAX_LINE_LEN);
    strncpy(new_line, line, MAX_LINE_LEN - 1);
    new_line[MAX_LINE_LEN - 1] = '\0';
    tab->lines[tab->line_count++] = new_line;
}

void tab_clear_screen(Tab *tab) {
    if (!tab) {
        return;
    }
    for (int k = 0; k < tab->line_count; k++) {
        if (tab->lines[k]) {
            free(tab->lines[k]);
            tab->lines[k] = NULL;
        }
    }
    tab->lines[0] = calloc(MAX_LINE_LEN, 1);
    tab->line_count = 1;
    tab->scroll_offset = 0;
}

void tab_append_output(Tab *tab, const char *bytes, int count) {
    if (!tab || !bytes || count <= 0) {
        return;
    }

    for (int i = 0; i < count; i++) {
        char c = bytes[i];

        if (tab->in_escape) {
            if (c == '\033') {
                tab->escape_len = 0;
                tab->escape_buf[tab->escape_len++] = c;
                continue;
            }
            if (tab->escape_len + 1 < (int)sizeof(tab->escape_buf)) {
                tab->escape_buf[tab->escape_len++] = c;
            }
            if (tab->escape_buf[1] == ']') {
                if (c == '\007' || c == '\n') {
                    tab->escape_buf[tab->escape_len] = '\0';
                    if (strncmp(tab->escape_buf, "\033]0;CWD:", 8) == 0) {
                        char *cwd_start = tab->escape_buf + 8;
                        char *cwd_end = strchr(cwd_start, '\007');
                        if (cwd_end) {
                            *cwd_end = '\0';
                        }
                        strncpy(tab->cwd, cwd_start, sizeof(tab->cwd) - 1);
                        tab->cwd[sizeof(tab->cwd) - 1] = '\0';
                    }
                    tab->in_escape = 0;
                    tab->escape_len = 0;
                }
                continue;
            }
            if (tab->escape_buf[1] == '[') {
                if (tab->escape_len > 2 && c >= '@' && c <= '~') {
                    tab->escape_buf[tab->escape_len] = '\0';
                    if (strstr(tab->escape_buf, "2J") != NULL || strstr(tab->escape_buf, "3J") != NULL) {
                        tab_clear_screen(tab);
                    }
                    tab->in_escape = 0;
                    tab->escape_len = 0;
                }
                continue;
            }
            if (tab->escape_len == 2 && tab->escape_buf[1] == 'c') {
                tab_clear_screen(tab);
                tab->in_escape = 0;
                tab->escape_len = 0;
                continue;
            }
            if (tab->escape_len > 3 || c == '\n') {
                tab->in_escape = 0;
                tab->escape_len = 0;
            }
            continue;
        }

        if (c == '\033') {
            tab->in_escape = 1;
            tab->escape_len = 0;
            tab->escape_buf[tab->escape_len++] = c;
            continue;
        }

        if (c == '\r') {
            continue;
        }

        if (c == '\n') {
            if (tab->line_count >= MAX_OUTPUT_LINES) {
                free(tab->lines[0]);
                for (int k = 1; k < tab->line_count; k++) {
                    tab->lines[k - 1] = tab->lines[k];
                }
                tab->line_count--;
            } else if (tab->line_count >= tab->line_capacity) {
                tab->line_capacity *= 2;
                tab->lines = realloc(tab->lines, sizeof(char *) * tab->line_capacity);
            }
            tab->lines[tab->line_count++] = calloc(MAX_LINE_LEN, 1);
            continue;
        }

        if (c == '\b') {
            if (tab->line_count > 0) {
                size_t len = strlen(tab->lines[tab->line_count - 1]);
                if (len > 0) {
                    tab->lines[tab->line_count - 1][len - 1] = '\0';
                }
            }
            continue;
        }

        if (tab->line_count == 0) {
            tab->lines[0] = calloc(MAX_LINE_LEN, 1);
            tab->line_count = 1;
        }

        size_t len = strlen(tab->lines[tab->line_count - 1]);
        if (len + 1 < MAX_LINE_LEN) {
            tab->lines[tab->line_count - 1][len] = c;
            tab->lines[tab->line_count - 1][len + 1] = '\0';
        }
    }
}

void tab_insert_char(Tab *tab, char c) {
    if (!tab || tab->input_len + 1 >= MAX_LINE_LEN) {
        return;
    }
    for (int i = tab->input_len; i > tab->cursor_pos; i--) {
        tab->input_line[i] = tab->input_line[i - 1];
    }
    tab->input_line[tab->cursor_pos] = c;
    tab->input_len++;
    tab->cursor_pos++;
    tab->input_line[tab->input_len] = '\0';
    tab->scroll_offset = 0;
    tab->history_browse_idx = -1;
}

void tab_backspace(Tab *tab) {
    if (!tab || tab->cursor_pos <= 0) {
        return;
    }
    for (int i = tab->cursor_pos - 1; i < tab->input_len - 1; i++) {
        tab->input_line[i] = tab->input_line[i + 1];
    }
    tab->input_len--;
    tab->cursor_pos--;
    tab->input_line[tab->input_len] = '\0';
    tab->scroll_offset = 0;
    tab->history_browse_idx = -1;
}

void tab_cursor_left(Tab *tab) {
    if (tab && tab->cursor_pos > 0) {
        tab->cursor_pos--;
    }
}

void tab_cursor_right(Tab *tab) {
    if (tab && tab->cursor_pos < tab->input_len) {
        tab->cursor_pos++;
    }
}

void tab_scroll(Tab *tab, int delta) {
    if (!tab) {
        return;
    }
    tab->scroll_offset += delta;
    if (tab->scroll_offset < 0) {
        tab->scroll_offset = 0;
    }
    if (tab->scroll_offset > tab->line_count) {
        tab->scroll_offset = tab->line_count;
    }
}

void tab_clear_input(Tab *tab) {
    if (!tab) {
        return;
    }
    tab->input_len = 0;
    tab->cursor_pos = 0;
    tab->input_line[0] = '\0';
    tab->scroll_offset = 0;
    tab->history_browse_idx = -1;
}
