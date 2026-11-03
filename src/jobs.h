#ifndef JOBS_H
#define JOBS_H

#include "common.h"

#define MAX_JOBS 32

typedef enum {
    JOB_EMPTY = 0,
    JOB_RUNNING,
    JOB_STOPPED,
    JOB_DONE
} JobState;

typedef struct {
    int id;
    pid_t pgid;
    pid_t pids[MAX_PIPE_CMDS];
    int nproc;
    char cmd[MAX_LINE_LEN];
    JobState state;
} Job;

void jobs_init(void);
int jobs_add(pid_t pgid, const pid_t *pids, int nproc, const char *cmd, JobState state);
void jobs_remove(int id);
Job *jobs_get(int id);
Job *jobs_get_last(void);
void jobs_update_status(void);
void jobs_print(int out_fd);
int jobs_bg(int id, int out_fd);
int jobs_fg(int id, int in_fd, int out_fd);

#endif
