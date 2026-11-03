#include "shell.h"
#include "execute.h"
#include "parse.h"
#include "jobs.h"
#include <ctype.h>

static int read_command_line(int in_fd, char *buf, size_t max_len) {
    size_t idx = 0;
    while (idx + 1 < max_len) {
        char c;
        int n = read(in_fd, &c, 1);
        if (n <= 0) {
            return idx > 0 ? (int)idx : -1;
        }
        if (c == '\n') {
            buf[idx] = '\0';
            int in_quotes = 0;
            if (!parse_is_incomplete_state(buf, &in_quotes)) {
                return (int)idx;
            }
            if (idx > 0 && buf[idx - 1] == '\\') {
                if (in_quotes) {
                    buf[idx - 1] = '\n';
                } else {
                    buf[idx - 1] = ' ';
                }
            } else {
                buf[idx++] = '\n';
            }
            continue;
        }
        if (c != '\r') {
            buf[idx++] = c;
        }
    }
    buf[idx] = '\0';
    return (int)idx;
}

static void print_shell_prompt(int out_fd, const char *cwd) {
    const char *home = getenv("HOME");
    char disp_cwd[1024];
    if (home && strncmp(cwd, home, strlen(home)) == 0) {
        snprintf(disp_cwd, sizeof(disp_cwd), "~%s", cwd + strlen(home));
    } else {
        snprintf(disp_cwd, sizeof(disp_cwd), "%s", cwd);
    }
    dprintf(out_fd, "\033]0;CWD:%s\007", cwd);
    dprintf(out_fd, "user@terminator:%s> ", disp_cwd);
}

void shell_run(int in_fd, int out_fd, const char *initial_cwd) {
    char cwd[1024];
    if (initial_cwd && strlen(initial_cwd) > 0) {
        strncpy(cwd, initial_cwd, sizeof(cwd) - 1);
        cwd[sizeof(cwd) - 1] = '\0';
    } else {
        if (!getcwd(cwd, sizeof(cwd))) {
            strcpy(cwd, "/");
        }
    }

    jobs_init();
    int last_status = 0;
    print_shell_prompt(out_fd, cwd);

    char line[MAX_LINE_LEN];
    while (1) {
        int len = read_command_line(in_fd, line, sizeof(line));
        if (len < 0) {
            break;
        }

        if (len == 1 && line[0] == '\x03') {
            dprintf(out_fd, "^C\n");
            jobs_update_status();
            print_shell_prompt(out_fd, cwd);
            continue;
        }

        if (len == 1 && line[0] == '\x1a') {
            dprintf(out_fd, "^Z\n");
            jobs_update_status();
            print_shell_prompt(out_fd, cwd);
            continue;
        }

        int all_space = 1;
        for (int i = 0; i < len; i++) {
            if (!isspace((unsigned char)line[i])) {
                all_space = 0;
                break;
            }
        }

        if (all_space) {
            jobs_update_status();
            print_shell_prompt(out_fd, cwd);
            continue;
        }

        int ret = execute_command_string(line, cwd, &last_status, in_fd, out_fd);
        if (ret == -99) {
            break;
        }

        jobs_update_status();
        print_shell_prompt(out_fd, cwd);
    }
    jobs_cleanup();
}

void *shell_thread_entry(void *arg) {
    ShellThreadArgs *args = (ShellThreadArgs *)arg;
    shell_run(args->in_fd, args->out_fd, args->initial_cwd);
    close(args->in_fd);
    close(args->out_fd);
    free(args);
    return NULL;
}

void shell_process_entry(int in_fd, int out_fd, const char *initial_cwd) {
    shell_run(in_fd, out_fd, initial_cwd);
    close(in_fd);
    close(out_fd);
    _exit(0);
}
