#include "jobs.h"

static __thread Job g_jobs[MAX_JOBS];
static __thread int g_next_job_id = 1;

void jobs_init(void) {
    memset(g_jobs, 0, sizeof(g_jobs));
    g_next_job_id = 1;
}

int jobs_add(pid_t pgid, const pid_t *pids, int nproc, const char *cmd, JobState state) {
    int slot = -1;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (g_jobs[i].state == JOB_EMPTY || g_jobs[i].state == JOB_DONE) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return -1;
    }
    g_jobs[slot].id = g_next_job_id++;
    g_jobs[slot].pgid = pgid;
    g_jobs[slot].nproc = nproc;
    for (int i = 0; i < nproc && i < MAX_PIPE_CMDS; i++) {
        g_jobs[slot].pids[i] = pids ? pids[i] : 0;
    }
    if (cmd) {
        strncpy(g_jobs[slot].cmd, cmd, sizeof(g_jobs[slot].cmd) - 1);
        g_jobs[slot].cmd[sizeof(g_jobs[slot].cmd) - 1] = '\0';
    } else {
        g_jobs[slot].cmd[0] = '\0';
    }
    g_jobs[slot].state = state;
    return g_jobs[slot].id;
}

void jobs_remove(int id) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (g_jobs[i].state != JOB_EMPTY && g_jobs[i].id == id) {
            g_jobs[i].state = JOB_EMPTY;
            break;
        }
    }
}

Job *jobs_get(int id) {
    if (id <= 0) {
        return jobs_get_last();
    }
    for (int i = 0; i < MAX_JOBS; i++) {
        if (g_jobs[i].state != JOB_EMPTY && g_jobs[i].id == id) {
            return &g_jobs[i];
        }
    }
    return NULL;
}

Job *jobs_get_last(void) {
    Job *last = NULL;
    int max_id = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (g_jobs[i].state != JOB_EMPTY && g_jobs[i].id > max_id) {
            max_id = g_jobs[i].id;
            last = &g_jobs[i];
        }
    }
    return last;
}

void jobs_update_status(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (g_jobs[i].state == JOB_RUNNING && g_jobs[i].nproc > 0) {
            int st = 0;
            pid_t w = waitpid(g_jobs[i].pids[g_jobs[i].nproc - 1], &st, WNOHANG);
            if (w > 0) {
                g_jobs[i].state = JOB_DONE;
            }
        }
    }
}

void jobs_print(int out_fd) {
    jobs_update_status();
    for (int i = 0; i < MAX_JOBS; i++) {
        if (g_jobs[i].state == JOB_RUNNING) {
            dprintf(out_fd, "[%d]+ Running %s &\n", g_jobs[i].id, g_jobs[i].cmd);
        } else if (g_jobs[i].state == JOB_STOPPED) {
            dprintf(out_fd, "[%d]+ Stopped %s\n", g_jobs[i].id, g_jobs[i].cmd);
        } else if (g_jobs[i].state == JOB_DONE) {
            dprintf(out_fd, "[%d]+ Done %s\n", g_jobs[i].id, g_jobs[i].cmd);
            g_jobs[i].state = JOB_EMPTY;
        }
    }
}

int jobs_bg(int id, int out_fd) {
    jobs_update_status();
    Job *j = jobs_get(id);
    if (!j) {
        dprintf(out_fd, "bg: no such job\n");
        return 1;
    }
    if (j->state == JOB_DONE) {
        dprintf(out_fd, "bg: job has terminated\n");
        j->state = JOB_EMPTY;
        return 1;
    }
    kill(-j->pgid, SIGCONT);
    kill(j->pgid, SIGCONT);
    j->state = JOB_RUNNING;
    dprintf(out_fd, "[%d]+ %s &\n", j->id, j->cmd);
    return 0;
}

int jobs_fg(int id, int in_fd, int out_fd) {
    jobs_update_status();
    Job *j = jobs_get(id);
    if (!j) {
        dprintf(out_fd, "fg: no such job\n");
        return 1;
    }
    if (j->state == JOB_DONE) {
        dprintf(out_fd, "fg: job has terminated\n");
        j->state = JOB_EMPTY;
        return 1;
    }
    dprintf(out_fd, "%s\n", j->cmd);
    if (j->state == JOB_STOPPED) {
        kill(-j->pgid, SIGCONT);
        kill(j->pgid, SIGCONT);
        j->state = JOB_RUNNING;
    }

    pid_t pgid = j->pgid;
    pid_t last_pid = j->pids[j->nproc - 1];
    char cmd_copy[MAX_LINE_LEN];
    snprintf(cmd_copy, sizeof(cmd_copy), "%s", j->cmd);
    int job_id = j->id;

    while (1) {
        int status = 0;
        pid_t w = waitpid(last_pid, &status, WNOHANG | WUNTRACED);
        if (w == last_pid) {
            if (WIFSTOPPED(status)) {
                j->state = JOB_STOPPED;
                dprintf(out_fd, "\n[%d]+ Stopped %s\n", job_id, cmd_copy);
                return 148;
            }
            j->state = JOB_EMPTY;
            if (WIFSIGNALED(status)) {
                return 128 + WTERMSIG(status);
            }
            return WEXITSTATUS(status);
        } else if (w < 0 && errno == ECHILD) {
            j->state = JOB_EMPTY;
            return 0;
        }

        if (in_fd >= 0) {
            struct pollfd pfd;
            pfd.fd = in_fd;
            pfd.events = POLLIN;
            pfd.revents = 0;
            int ret = poll(&pfd, 1, 50);
            if (ret > 0 && (pfd.revents & POLLIN)) {
                char c;
                int n = read(in_fd, &c, 1);
                if (n > 0) {
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
}

void jobs_cleanup(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (g_jobs[i].state == JOB_RUNNING || g_jobs[i].state == JOB_STOPPED) {
            kill(-g_jobs[i].pgid, SIGHUP);
            kill(-g_jobs[i].pgid, SIGCONT);
            kill(g_jobs[i].pgid, SIGHUP);
            kill(g_jobs[i].pgid, SIGCONT);
            g_jobs[i].state = JOB_EMPTY;
        }
    }
}
