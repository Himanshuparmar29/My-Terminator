#ifndef HISTORY_H
#define HISTORY_H

#include "common.h"

typedef struct {
    char entries[HISTORY_CAPACITY][MAX_LINE_LEN];
    int count;
    pthread_mutex_t thread_mutex;
    sem_t proc_sem;
} SharedHistory;

void history_init(ExecutionMode mode);
void history_cleanup(void);
void history_add(const char *cmd);
void history_print(int out_fd);
int history_get_count(void);
int history_get_entry(int index, char *buf, size_t max_len);
int history_search(const char *term, char results[][MAX_LINE_LEN], int max_results);

#endif
