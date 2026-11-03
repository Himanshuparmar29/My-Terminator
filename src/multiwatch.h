#ifndef MULTIWATCH_H
#define MULTIWATCH_H

#include "common.h"

int multiwatch_is_command(const char *cmd_line);
int multiwatch_execute(const char *cmd_line, const char *cwd, int cancel_fd, int out_fd);

#endif
