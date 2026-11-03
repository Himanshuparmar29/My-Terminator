#include "execute.h"
#include "multiwatch.h"
#include "history.h"
#include <ctype.h>

int execute_pipeline_full(Pipeline *pl, const char *cwd, int last_status, int in_fd, int out_fd, int is_bg, const char *orig_cmd) {
    (void)last_status;
    if (!pl || pl->command_count == 0) {
        return 0;
    }

    int n = pl->command_count;
    int pipe_fds[MAX_PIPE_CMDS - 1][2];

    for (int i = 0; i < n - 1; i++) {
        if (pipe2(pipe_fds[i], O_CLOEXEC) < 0) {
            perror("pipe2");
            return 1;
        }
    }

    pid_t pids[MAX_PIPE_CMDS];
    pid_t pgid = 0;

    for (int i = 0; i < n; i++) {
        SingleCommand *cmd = &pl->commands[i];
        pids[i] = fork();

        if (pids[i] < 0) {
            perror("fork");
            return 1;
        }

        if (pids[i] == 0) {
            if (i == 0) {
                setpgid(0, 0);
            } else {
                setpgid(0, pgid);
            }

            if (cwd && strlen(cwd) > 0) {
                if (chdir(cwd) != 0) {
                }
            }

            if (i == 0) {
                if (cmd->input_file) {
                    int in_f = open(cmd->input_file, O_RDONLY);
                    if (in_f < 0) {
                        dprintf(STDERR_FILENO, "terminator: %s: No such file or directory\n", cmd->input_file);
                        _exit(1);
                    }
                    dup2(in_f, STDIN_FILENO);
                    close(in_f);
                }
            } else {
                dup2(pipe_fds[i - 1][0], STDIN_FILENO);
            }

            if (i == n - 1) {
                if (cmd->output_file) {
                    int out_f = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (out_f < 0) {
                        perror("open");
                        _exit(1);
                    }
                    dup2(out_f, STDOUT_FILENO);
                    close(out_f);
                } else if (out_fd >= 0 && out_fd != STDOUT_FILENO) {
                    dup2(out_fd, STDOUT_FILENO);
                    dup2(out_fd, STDERR_FILENO);
                }
            } else {
                dup2(pipe_fds[i][1], STDOUT_FILENO);
            }

            for (int j = 0; j < n - 1; j++) {
                close(pipe_fds[j][0]);
                close(pipe_fds[j][1]);
            }

            int max_fd = sysconf(_SC_OPEN_MAX);
            if (max_fd < 0 || max_fd > 1024) max_fd = 1024;
            for (int fd = 3; fd < max_fd; fd++) {
                close(fd);
            }

            if (cmd->argc == 0) {
                _exit(0);
            }

            if (strcmp(cmd->argv[0], "history") == 0) {
                history_print(STDOUT_FILENO);
                _exit(0);
            }

            if (strcmp(cmd->argv[0], "jobs") == 0) {
                jobs_print(STDOUT_FILENO);
                _exit(0);
            }

            if (strcmp(cmd->argv[0], "pwd") == 0) {
                if (cwd) {
                    dprintf(STDOUT_FILENO, "%s\n", cwd);
                }
                _exit(0);
            }

            if (strcmp(cmd->argv[0], "exit") == 0) {
                _exit(0);
            }

            execvp(cmd->argv[0], cmd->argv);
            dprintf(STDERR_FILENO, "terminator: %s: command not found\n", cmd->argv[0]);
            _exit(127);
        }

        if (i == 0) {
            pgid = pids[0];
            setpgid(pids[0], pids[0]);
        } else {
            setpgid(pids[i], pgid);
        }
    }

    for (int j = 0; j < n - 1; j++) {
        close(pipe_fds[j][0]);
        close(pipe_fds[j][1]);
    }

    if (is_bg) {
        int jid = jobs_add(pgid, pids, n, orig_cmd, JOB_RUNNING);
        dprintf(out_fd, "[%d] %d\n", jid, (int)pgid);
        return 0;
    }

    pid_t last_pid = pids[n - 1];
    int exit_status = 0;

    while (1) {
        int status = 0;
        pid_t w = waitpid(last_pid, &status, WNOHANG | WUNTRACED);
        if (w == last_pid) {
            if (WIFSTOPPED(status)) {
                int jid = jobs_add(pgid, pids, n, orig_cmd, JOB_STOPPED);
                dprintf(out_fd, "\n[%d]+ Stopped %s\n", jid, orig_cmd ? orig_cmd : "");
                return 148;
            }
            if (WIFSIGNALED(status)) {
                exit_status = 128 + WTERMSIG(status);
            } else if (WIFEXITED(status)) {
                exit_status = WEXITSTATUS(status);
            }
            break;
        } else if (w < 0 && errno == ECHILD) {
            break;
        }

        if (in_fd >= 0) {
            struct pollfd pfd;
            pfd.fd = in_fd;
            pfd.events = POLLIN;
            pfd.revents = 0;
            int ret = poll(&pfd, 1, 50);
            if (ret > 0 && (pfd.revents & POLLIN)) {
                char c;
                int nr = read(in_fd, &c, 1);
                if (nr > 0) {
                    if (c == 3 || c == '\x03') {
                        kill(-pgid, SIGINT);
                        kill(pgid, SIGINT);
                        dprintf(out_fd, "^C\n");
                    } else if (c == 26 || c == '\x1a') {
                        kill(-pgid, SIGTSTP);
                        kill(pgid, SIGTSTP);
                        dprintf(out_fd, "^Z\n");
                    }
                    struct pollfd pfd_drain;
                    pfd_drain.fd = in_fd;
                    pfd_drain.events = POLLIN;
                    while (poll(&pfd_drain, 1, 0) > 0 && (pfd_drain.revents & POLLIN)) {
                        char extra;
                        if (read(in_fd, &extra, 1) <= 0 || (extra != '\n' && extra != '\r')) {
                            break;
                        }
                    }
                }
            }
        } else {
            usleep(50000);
        }
    }

    for (int i = 0; i < n - 1; i++) {
        waitpid(pids[i], NULL, 0);
    }

    return exit_status;
}

int execute_pipeline(Pipeline *pl, const char *cwd, int last_status, int out_fd) {
    return execute_pipeline_full(pl, cwd, last_status, -1, out_fd, 0, NULL);
}

int execute_capture(const char *cmd_line, const char *cwd, int last_status, char *out_buf, size_t max_len) {
    if (!cmd_line || !out_buf || max_len == 0) {
        return 1;
    }
    out_buf[0] = '\0';

    int pipe_fd[2];
    if (pipe2(pipe_fd, O_CLOEXEC) < 0) {
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return 1;
    }

    if (pid == 0) {
        dup2(pipe_fd[1], STDOUT_FILENO);
        close(pipe_fd[0]);
        close(pipe_fd[1]);

        if (cwd && strlen(cwd) > 0) {
            if (chdir(cwd) != 0) {
            }
        }

        Pipeline pl;
        if (parse_pipeline(cmd_line, &pl) > 0) {
            execute_pipeline(&pl, cwd, last_status, STDOUT_FILENO);
            pipeline_free(&pl);
        }
        _exit(0);
    }

    close(pipe_fd[1]);

    size_t total_read = 0;
    while (total_read + 1 < max_len) {
        int n = read(pipe_fd[0], out_buf + total_read, max_len - 1 - total_read);
        if (n <= 0) {
            break;
        }
        total_read += n;
    }
    out_buf[total_read] = '\0';
    close(pipe_fd[0]);
    waitpid(pid, NULL, 0);
    return 0;
}

int execute_command_string(const char *cmd_line, char *cwd, int *last_status, int cancel_fd, int out_fd) {
    if (!cmd_line) {
        return 0;
    }
    const char *p = cmd_line;
    while (isspace((unsigned char)*p)) {
        p++;
    }
    if (*p == '\0') {
        return 0;
    }

    history_add(cmd_line);

    char expanded[MAX_LINE_LEN];
    parse_expand_variables(cmd_line, expanded, sizeof(expanded), *last_status);

    char subst_done[MAX_LINE_LEN];
    parse_expand_substitutions(expanded, subst_done, sizeof(subst_done), cwd, *last_status);

    if (multiwatch_is_command(subst_done)) {
        *last_status = multiwatch_execute(subst_done, cwd, cancel_fd, out_fd);
        return 0;
    }

    int is_bg = 0;
    size_t slen = strlen(subst_done);
    while (slen > 0 && isspace((unsigned char)subst_done[slen - 1])) {
        slen--;
    }
    char clean_cmd[MAX_LINE_LEN];
    if (slen > 0 && subst_done[slen - 1] == '&') {
        is_bg = 1;
        size_t k = slen - 1;
        while (k > 0 && isspace((unsigned char)subst_done[k - 1])) {
            k--;
        }
        memcpy(clean_cmd, subst_done, k);
        clean_cmd[k] = '\0';
    } else {
        strncpy(clean_cmd, subst_done, sizeof(clean_cmd) - 1);
        clean_cmd[sizeof(clean_cmd) - 1] = '\0';
    }

    Pipeline pl;
    int count = parse_pipeline(clean_cmd, &pl);
    if (count == 0) {
        return 0;
    }

    if (count == 1 && pl.commands[0].argc > 0) {
        char *first = pl.commands[0].argv[0];
        if (strcmp(first, "jobs") == 0) {
            jobs_print(out_fd);
            *last_status = 0;
            pipeline_free(&pl);
            return 0;
        }

        if (strcmp(first, "bg") == 0) {
            int jid = -1;
            if (pl.commands[0].argc > 1) {
                char *arg = pl.commands[0].argv[1];
                if (arg[0] == '%') arg++;
                jid = atoi(arg);
            }
            *last_status = jobs_bg(jid, out_fd);
            pipeline_free(&pl);
            return 0;
        }

        if (strcmp(first, "fg") == 0) {
            int jid = -1;
            if (pl.commands[0].argc > 1) {
                char *arg = pl.commands[0].argv[1];
                if (arg[0] == '%') arg++;
                jid = atoi(arg);
            }
            *last_status = jobs_fg(jid, cancel_fd, out_fd);
            pipeline_free(&pl);
            return 0;
        }

        if (strcmp(first, "cd") == 0) {
            const char *target = pl.commands[0].argc > 1 ? pl.commands[0].argv[1] : NULL;
            char target_buf[1024];
            const char *home = getenv("HOME");
            if (!home) {
                home = "/";
            }

            if (!target || strcmp(target, "~") == 0) {
                snprintf(target_buf, sizeof(target_buf), "%s", home);
            } else if (target[0] == '~' && (target[1] == '/' || target[1] == '\0')) {
                snprintf(target_buf, sizeof(target_buf), "%s%s", home, target + 1);
            } else {
                snprintf(target_buf, sizeof(target_buf), "%s", target);
            }

            char resolved[2048];
            if (target_buf[0] == '/') {
                snprintf(resolved, sizeof(resolved), "%s", target_buf);
            } else {
                snprintf(resolved, sizeof(resolved), "%s/%s", cwd, target_buf);
            }

            char *rp = realpath(resolved, NULL);
            if (rp != NULL) {
                struct stat st;
                if (stat(rp, &st) == 0 && S_ISDIR(st.st_mode)) {
                    snprintf(cwd, 1024, "%s", rp);
                    *last_status = 0;
                } else {
                    dprintf(out_fd, "cd: %s: Not a directory\n", target);
                    *last_status = 1;
                }
                free(rp);
            } else {
                dprintf(out_fd, "cd: %s: No such file or directory\n", target);
                *last_status = 1;
            }
            pipeline_free(&pl);
            return 0;
        }

        if (strcmp(first, "exit") == 0) {
            pipeline_free(&pl);
            return -99;
        }

        if (strcmp(first, "clear") == 0) {
            dprintf(out_fd, "\033[H\033[2J\033[3J");
            *last_status = 0;
            pipeline_free(&pl);
            return 0;
        }

        if (strcmp(first, "history") == 0 && !pl.commands[0].output_file) {
            history_print(out_fd);
            *last_status = 0;
            pipeline_free(&pl);
            return 0;
        }
    }

    *last_status = execute_pipeline_full(&pl, cwd, *last_status, cancel_fd, out_fd, is_bg, clean_cmd);
    pipeline_free(&pl);
    return 0;
}
