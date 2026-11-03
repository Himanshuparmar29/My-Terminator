#ifndef PARSE_H
#define PARSE_H

#include "common.h"

typedef struct {
    char *argv[MAX_ARGS];
    int argc;
    char *input_file;
    char *output_file;
} SingleCommand;

typedef struct {
    SingleCommand commands[MAX_PIPE_CMDS];
    int command_count;
} Pipeline;

int parse_is_incomplete(const char *input);
int parse_is_incomplete_state(const char *input, int *out_in_quotes);
void parse_expand_variables(const char *input, char *output, size_t out_size, int last_status);
int parse_expand_substitutions(const char *input, char *output, size_t out_size, const char *cwd, int last_status);
int parse_pipeline(const char *input, Pipeline *pl);
void pipeline_free(Pipeline *pl);

#endif
