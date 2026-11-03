#ifndef EXECUTE_H
#define EXECUTE_H

#include "common.h"
#include "parse.h"
#include "jobs.h"

int execute_pipeline(Pipeline *pl, const char *cwd, int last_status, int out_fd);
int execute_pipeline_full(Pipeline *pl, const char *cwd, int last_status, int in_fd, int out_fd, int is_bg, const char *orig_cmd);
int execute_capture(const char *cmd_line, const char *cwd, int last_status, char *out_buf, size_t max_len);
int execute_command_string(const char *cmd_line, char *cwd, int *last_status, int cancel_fd, int out_fd);

#endif
