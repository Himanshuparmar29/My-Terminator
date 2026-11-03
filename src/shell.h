#ifndef SHELL_H
#define SHELL_H

#include "common.h"

typedef struct {
    int in_fd;
    int out_fd;
    char initial_cwd[1024];
    int tab_id;
} ShellThreadArgs;

void shell_run(int in_fd, int out_fd, const char *initial_cwd);
void *shell_thread_entry(void *arg);
void shell_process_entry(int in_fd, int out_fd, const char *initial_cwd);

#endif
