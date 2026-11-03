#include "multiwatch.h"
#include "parse.h"
#include "execute.h"

#define MAX_WATCH_CMDS 16

typedef struct {
    char cmd_str[MAX_LINE_LEN];
    pid_t pid;
    int read_fd;
    time_t finish_time;
    int is_running;
} WatchEntry;

static const char *SEPARATOR = "----------------------------------------------------";

int multiwatch_is_command(const char *cmd_line) {
    if (!cmd_line) {
        return 0;
    }
    while (*cmd_line == ' ' || *cmd_line == '\t') {
        cmd_line++;
    }
    if (strncmp(cmd_line, "multiWatch", 10) == 0) {
        char c = cmd_line[10];
        if (c == ' ' || c == '\t' || c == '[' || c == '\0') {
            return 1;
        }
    }
    return 0;
}

static int parse_watch_list(const char *cmd_line, WatchEntry entries[], int max_entries) {
    const char *p = strchr(cmd_line, '[');
    if (!p) {
        return 0;
    }
    p++;
    int count = 0;

    while (*p != '\0' && *p != ']' && count < max_entries) {
        while (*p == ' ' || *p == '\t' || *p == ',') {
            p++;
        }
        if (*p == ']' || *p == '\0') {
            break;
        }

        if (*p == '"' || *p == '\'') {
            char quote = *p++;
            char buf[MAX_LINE_LEN];
            size_t b_idx = 0;
            while (*p != '\0' && *p != quote) {
                if (*p == '\\' && *(p + 1) != '\0') {
                    p++;
                }
                if (b_idx + 1 < sizeof(buf)) {
                    buf[b_idx++] = *p;
                }
                p++;
            }
            if (*p == quote) {
                p++;
            }
            buf[b_idx] = '\0';
            if (b_idx > 0) {
                strncpy(entries[count].cmd_str, buf, MAX_LINE_LEN - 1);
                entries[count].cmd_str[MAX_LINE_LEN - 1] = '\0';
                entries[count].pid = 0;
                entries[count].read_fd = -1;
                entries[count].finish_time = 0;
                entries[count].is_running = 0;
                count++;
            }
        } else {
            p++;
        }
    }

    return count;
}

static void launch_watch_cmd(WatchEntry *entry, const char *cwd) {
    int pipe_fd[2];
    if (pipe(pipe_fd) < 0) {
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return;
    }

    if (pid == 0) {
        setpgid(0, 0);
        dup2(pipe_fd[1], STDOUT_FILENO);
        dup2(pipe_fd[1], STDERR_FILENO);
        close(pipe_fd[0]);
        close(pipe_fd[1]);

        if (cwd && strlen(cwd) > 0) {
            if (chdir(cwd) != 0) {
            }
        }

        char expanded[MAX_LINE_LEN];
        parse_expand_variables(entry->cmd_str, expanded, sizeof(expanded), 0);

        char subst_done[MAX_LINE_LEN];
        parse_expand_substitutions(expanded, subst_done, sizeof(subst_done), cwd, 0);

        Pipeline pl;
        if (parse_pipeline(subst_done, &pl) > 0) {
            execute_pipeline(&pl, cwd, 0, STDOUT_FILENO);
            pipeline_free(&pl);
        }
        _exit(0);
    }

    close(pipe_fd[1]);
    entry->read_fd = pipe_fd[0];
    entry->pid = pid;
    entry->is_running = 1;
}

int multiwatch_execute(const char *cmd_line, const char *cwd, int cancel_fd, int out_fd) {
    WatchEntry entries[MAX_WATCH_CMDS];
    int count = parse_watch_list(cmd_line, entries, MAX_WATCH_CMDS);
    if (count == 0) {
        dprintf(out_fd, "multiWatch: invalid syntax. Usage: multiWatch [\"cmd1\", \"cmd2\", ...]\n");
        return 1;
    }

    for (int i = 0; i < count; i++) {
        launch_watch_cmd(&entries[i], cwd);
    }

    int active = 1;
    while (active) {
        struct pollfd fds[MAX_WATCH_CMDS + 1];
        int nfds = 0;

        if (cancel_fd >= 0) {
            fds[nfds].fd = cancel_fd;
            fds[nfds].events = POLLIN;
            fds[nfds].revents = 0;
            nfds++;
        }

        int entry_to_fd[MAX_WATCH_CMDS];
        for (int i = 0; i < count; i++) {
            if (entries[i].is_running && entries[i].read_fd >= 0) {
                fds[nfds].fd = entries[i].read_fd;
                fds[nfds].events = POLLIN;
                fds[nfds].revents = 0;
                entry_to_fd[nfds] = i;
                nfds++;
            }
        }

        int ret = poll(fds, nfds, 100);
        if (ret < 0 && errno == EINTR) {
            continue;
        }

        if (cancel_fd >= 0 && (fds[0].revents & (POLLIN | POLLHUP))) {
            char ctrl_buf[32];
            int n = read(cancel_fd, ctrl_buf, sizeof(ctrl_buf));
            if (n > 0) {
                for (int c = 0; c < n; c++) {
                    if (ctrl_buf[c] == 3 || ctrl_buf[c] == '\x03') {
                        active = 0;
                        break;
                    }
                }
            } else if (n == 0) {
                active = 0;
            }
            if (!active) {
                dprintf(out_fd, "^C\n");
                break;
            }
        }

        int start_idx = (cancel_fd >= 0) ? 1 : 0;
        for (int f = start_idx; f < nfds; f++) {
            int e_idx = entry_to_fd[f];
            if (fds[f].revents & POLLIN) {
                char read_buf[4096];
                int bytes_read = read(entries[e_idx].read_fd, read_buf, sizeof(read_buf) - 1);
                if (bytes_read > 0) {
                    read_buf[bytes_read] = '\0';
                    time_t ts = time(NULL);
                    dprintf(out_fd, "\"%s\", %ld:\n%s\n", entries[e_idx].cmd_str, (long)ts, SEPARATOR);
                    ssize_t w = write(out_fd, read_buf, bytes_read);
                    (void)w;
                    if (read_buf[bytes_read - 1] != '\n') {
                        dprintf(out_fd, "\n");
                    }
                    dprintf(out_fd, "%s\n", SEPARATOR);
                } else if (bytes_read == 0) {
                    close(entries[e_idx].read_fd);
                    entries[e_idx].read_fd = -1;
                    if (entries[e_idx].pid > 0) {
                        waitpid(entries[e_idx].pid, NULL, WNOHANG);
                        entries[e_idx].pid = 0;
                    }
                    entries[e_idx].is_running = 0;
                    entries[e_idx].finish_time = time(NULL);
                }
            } else if (fds[f].revents & (POLLHUP | POLLERR)) {
                if (entries[e_idx].read_fd >= 0) {
                    close(entries[e_idx].read_fd);
                    entries[e_idx].read_fd = -1;
                }
                if (entries[e_idx].pid > 0) {
                    waitpid(entries[e_idx].pid, NULL, WNOHANG);
                    entries[e_idx].pid = 0;
                }
                entries[e_idx].is_running = 0;
                entries[e_idx].finish_time = time(NULL);
            }
        }

        time_t current_time = time(NULL);
        for (int i = 0; i < count; i++) {
            if (!entries[i].is_running) {
                if (current_time - entries[i].finish_time >= 2) {
                    launch_watch_cmd(&entries[i], cwd);
                }
            }
        }
    }

    for (int i = 0; i < count; i++) {
        if (entries[i].is_running && entries[i].pid > 0) {
            kill(-entries[i].pid, SIGINT);
            kill(entries[i].pid, SIGINT);
            usleep(20000);
            kill(-entries[i].pid, SIGKILL);
            kill(entries[i].pid, SIGKILL);
            waitpid(entries[i].pid, NULL, 0);
        }
        if (entries[i].read_fd >= 0) {
            close(entries[i].read_fd);
            entries[i].read_fd = -1;
        }
    }

    return 0;
}