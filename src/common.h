#ifndef COMMON_H
#define COMMON_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <pthread.h>
#include <semaphore.h>

#define MAX_LINE_LEN 4096
#define MAX_ARGS 256
#define MAX_PIPE_CMDS 64
#define MAX_TABS 8
#define MAX_OUTPUT_LINES 10000
#define HISTORY_CAPACITY 10000

typedef enum {
    MODE_THREAD = 0,
    MODE_PROCESS = 1
} ExecutionMode;

extern ExecutionMode global_exec_mode;

#endif
