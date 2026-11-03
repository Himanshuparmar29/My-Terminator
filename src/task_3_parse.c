#include "parse.h"
#include "execute.h"
#include <ctype.h>

int parse_is_incomplete_state(const char *input, int *out_in_quotes) {
    if (!input) {
        if (out_in_quotes) {
            *out_in_quotes = 0;
        }
        return 0;
    }
    int in_single = 0;
    int in_double = 0;
    int paren_depth = 0;
    int in_backticks = 0;
    int escaped = 0;

    for (size_t i = 0; input[i] != '\0'; i++) {
        char c = input[i];
        if (in_single) {
            if (c == '\'') {
                in_single = 0;
            }
            continue;
        }
        if (escaped) {
            escaped = 0;
            continue;
        }
        if (c == '\\') {
            escaped = 1;
            continue;
        }
        if (in_double) {
            if (c == '"') {
                in_double = 0;
            } else if (c == '`') {
                in_backticks = !in_backticks;
            } else if (c == '$' && input[i + 1] == '(') {
                paren_depth++;
                i++;
            } else if (c == ')' && paren_depth > 0) {
                paren_depth--;
            }
            continue;
        }
        if (c == '\'') {
            in_single = 1;
        } else if (c == '"') {
            in_double = 1;
        } else if (c == '`') {
            in_backticks = !in_backticks;
        } else if (c == '$' && input[i + 1] == '(') {
            paren_depth++;
            i++;
        } else if (c == ')' && paren_depth > 0) {
            paren_depth--;
        }
    }

    if (out_in_quotes) {
        *out_in_quotes = (in_single || in_double) && (paren_depth == 0) && !in_backticks;
    }

    if (escaped || in_single || in_double || paren_depth > 0 || in_backticks) {
        return 1;
    }

    if (!escaped && !in_single && !in_double && paren_depth == 0 && !in_backticks) {
        size_t k = strlen(input);
        while (k > 0 && isspace((unsigned char)input[k - 1])) {
            k--;
        }
        if (k > 0 && input[k - 1] == '|') {
            return 1;
        }
    }

    return 0;
}

int parse_is_incomplete(const char *input) {
    return parse_is_incomplete_state(input, NULL);
}

void parse_expand_variables(const char *input, char *output, size_t out_size, int last_status) {
    if (!input || !output || out_size == 0) {
        return;
    }
    size_t out_idx = 0;
    int in_single = 0;
    int in_double = 0;
    int escaped = 0;

    for (size_t i = 0; input[i] != '\0' && out_idx + 1 < out_size; i++) {
        char c = input[i];

        if (in_single) {
            if (c == '\'') {
                in_single = 0;
            }
            output[out_idx++] = c;
            continue;
        }

        if (escaped) {
            escaped = 0;
            output[out_idx++] = c;
            continue;
        }

        if (c == '\\') {
            escaped = 1;
            output[out_idx++] = c;
            continue;
        }

        if (c == '\'') {
            if (!in_double) {
                in_single = 1;
            }
            output[out_idx++] = c;
            continue;
        }

        if (c == '"') {
            in_double = !in_double;
            output[out_idx++] = c;
            continue;
        }

        if (c == '$') {
            char next = input[i + 1];
            if (next == '?') {
                char num_buf[32];
                snprintf(num_buf, sizeof(num_buf), "%d", last_status);
                for (size_t k = 0; num_buf[k] != '\0' && out_idx + 1 < out_size; k++) {
                    output[out_idx++] = num_buf[k];
                }
                i++;
                continue;
            }
            if (next == '{') {
                size_t start = i + 2;
                size_t end = start;
                while (input[end] != '\0' && input[end] != '}') {
                    end++;
                }
                if (input[end] == '}') {
                    char var_name[256];
                    size_t vlen = end - start;
                    if (vlen >= sizeof(var_name)) {
                        vlen = sizeof(var_name) - 1;
                    }
                    memcpy(var_name, &input[start], vlen);
                    var_name[vlen] = '\0';
                    const char *val = getenv(var_name);
                    if (val) {
                        for (size_t k = 0; val[k] != '\0' && out_idx + 1 < out_size; k++) {
                            output[out_idx++] = val[k];
                        }
                    }
                    i = end;
                    continue;
                }
            }
            if (isalnum((unsigned char)next) || next == '_') {
                size_t start = i + 1;
                size_t end = start;
                while (isalnum((unsigned char)input[end]) || input[end] == '_') {
                    end++;
                }
                char var_name[256];
                size_t vlen = end - start;
                if (vlen >= sizeof(var_name)) {
                    vlen = sizeof(var_name) - 1;
                }
                memcpy(var_name, &input[start], vlen);
                var_name[vlen] = '\0';
                const char *val = getenv(var_name);
                if (val) {
                    for (size_t k = 0; val[k] != '\0' && out_idx + 1 < out_size; k++) {
                        output[out_idx++] = val[k];
                    }
                }
                i = end - 1;
                continue;
            }
        }

        output[out_idx++] = c;
    }

    output[out_idx] = '\0';
}

static int find_innermost_subst(const char *str, int *subst_type, size_t *start_idx, size_t *end_idx) {
    size_t len = strlen(str);
    int best_type = 0;
    size_t best_start = 0;
    size_t best_end = 0;
    int found = 0;

    int in_single = 0;
    int escaped = 0;

    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        if (in_single) {
            if (c == '\'') {
                in_single = 0;
            }
            continue;
        }
        if (escaped) {
            escaped = 0;
            continue;
        }
        if (c == '\\') {
            escaped = 1;
            continue;
        }
        if (c == '\'') {
            in_single = 1;
            continue;
        }

        if (c == '$' && i + 1 < len && str[i + 1] == '(') {
            size_t pstart = i;
            size_t inner = i + 2;
            int depth = 1;
            int inner_single = 0;
            int inner_esc = 0;
            while (inner < len && depth > 0) {
                if (inner_single) {
                    if (str[inner] == '\'') {
                        inner_single = 0;
                    }
                } else if (inner_esc) {
                    inner_esc = 0;
                } else if (str[inner] == '\\') {
                    inner_esc = 1;
                } else if (str[inner] == '\'') {
                    inner_single = 1;
                } else if (str[inner] == '$' && inner + 1 < len && str[inner + 1] == '(') {
                    pstart = inner;
                    depth = 1;
                    inner++;
                } else if (str[inner] == '(') {
                    depth++;
                } else if (str[inner] == ')') {
                    depth--;
                    if (depth == 0) {
                        best_type = 1;
                        best_start = pstart;
                        best_end = inner;
                        found = 1;
                        break;
                    }
                }
                inner++;
            }
            if (found) {
                break;
            }
        } else if (c == '`') {
            size_t bstart = i;
            size_t inner = i + 1;
            int inner_single = 0;
            int inner_esc = 0;
            while (inner < len) {
                if (inner_single) {
                    if (str[inner] == '\'') {
                        inner_single = 0;
                    }
                } else if (inner_esc) {
                    inner_esc = 0;
                } else if (str[inner] == '\\') {
                    inner_esc = 1;
                } else if (str[inner] == '\'') {
                    inner_single = 1;
                } else if (str[inner] == '`') {
                    best_type = 2;
                    best_start = bstart;
                    best_end = inner;
                    found = 1;
                    break;
                }
                inner++;
            }
            if (found) {
                break;
            }
        }
    }

    if (found) {
        *subst_type = best_type;
        *start_idx = best_start;
        *end_idx = best_end;
        return 1;
    }
    return 0;
}

int parse_expand_substitutions(const char *input, char *output, size_t out_size, const char *cwd, int last_status) {
    if (!input || !output || out_size == 0) {
        return 0;
    }

    char current[MAX_LINE_LEN * 4];
    strncpy(current, input, sizeof(current) - 1);
    current[sizeof(current) - 1] = '\0';

    int subst_type = 0;
    size_t start_idx = 0;
    size_t end_idx = 0;
    int max_iterations = 20;

    while (max_iterations-- > 0 && find_innermost_subst(current, &subst_type, &start_idx, &end_idx)) {
        char inner_cmd[MAX_LINE_LEN];
        size_t cmd_len = 0;
        if (subst_type == 1) {
            cmd_len = (end_idx > start_idx + 2) ? (end_idx - (start_idx + 2)) : 0;
            if (cmd_len >= sizeof(inner_cmd)) {
                cmd_len = sizeof(inner_cmd) - 1;
            }
            memcpy(inner_cmd, &current[start_idx + 2], cmd_len);
            inner_cmd[cmd_len] = '\0';
        } else {
            cmd_len = (end_idx > start_idx + 1) ? (end_idx - (start_idx + 1)) : 0;
            if (cmd_len >= sizeof(inner_cmd)) {
                cmd_len = sizeof(inner_cmd) - 1;
            }
            memcpy(inner_cmd, &current[start_idx + 1], cmd_len);
            inner_cmd[cmd_len] = '\0';
        }

        char captured[MAX_LINE_LEN];
        captured[0] = '\0';
        execute_capture(inner_cmd, cwd, last_status, captured, sizeof(captured));

        size_t cap_len = strlen(captured);
        while (cap_len > 0 && (captured[cap_len - 1] == '\n' || captured[cap_len - 1] == '\r')) {
            captured[--cap_len] = '\0';
        }

        char next_str[MAX_LINE_LEN * 4];
        size_t next_idx = 0;

        for (size_t i = 0; i < start_idx && next_idx + 1 < sizeof(next_str); i++) {
            next_str[next_idx++] = current[i];
        }
        for (size_t i = 0; i < cap_len && next_idx + 1 < sizeof(next_str); i++) {
            next_str[next_idx++] = captured[i];
        }
        for (size_t i = end_idx + 1; current[i] != '\0' && next_idx + 1 < sizeof(next_str); i++) {
            next_str[next_idx++] = current[i];
        }
        next_str[next_idx] = '\0';
        snprintf(current, sizeof(current), "%s", next_str);
    }

    snprintf(output, out_size, "%s", current);
    return 1;
}

static char *extract_word(const char *str, size_t *idx) {
    while (isspace((unsigned char)str[*idx])) {
        (*idx)++;
    }
    if (str[*idx] == '\0') {
        return NULL;
    }

    char buf[MAX_LINE_LEN];
    size_t b_idx = 0;
    int in_single = 0;
    int in_double = 0;
    int escaped = 0;

    while (str[*idx] != '\0') {
        char c = str[*idx];

        if (escaped) {
            escaped = 0;
            if (b_idx + 1 < sizeof(buf)) {
                buf[b_idx++] = c;
            }
            (*idx)++;
            continue;
        }

        if (c == '\\') {
            if (!in_single && !in_double) {
                escaped = 1;
                (*idx)++;
                continue;
            } else if (in_double) {
                char next_c = str[*idx + 1];
                if (next_c == '"' || next_c == '\\' || next_c == '$' || next_c == '`') {
                    escaped = 1;
                    (*idx)++;
                    continue;
                }
            }
        }

        if (c == '\'' && !in_double) {
            in_single = !in_single;
            (*idx)++;
            continue;
        }

        if (c == '"' && !in_single) {
            in_double = !in_double;
            (*idx)++;
            continue;
        }

        if (!in_single && !in_double) {
            if (isspace((unsigned char)c) || c == '<' || c == '>' || c == '|') {
                break;
            }
        }

        if (b_idx + 1 < sizeof(buf)) {
            buf[b_idx++] = c;
        }
        (*idx)++;
    }

    buf[b_idx] = '\0';
    return strdup(buf);
}

static int parse_single_command(const char *cmd_str, SingleCommand *cmd) {
    cmd->argc = 0;
    cmd->input_file = NULL;
    cmd->output_file = NULL;
    for (int i = 0; i < MAX_ARGS; i++) {
        cmd->argv[i] = NULL;
    }

    size_t idx = 0;
    while (cmd_str[idx] != '\0') {
        while (isspace((unsigned char)cmd_str[idx])) {
            idx++;
        }
        if (cmd_str[idx] == '\0') {
            break;
        }

        if (cmd_str[idx] == '<') {
            idx++;
            char *fname = extract_word(cmd_str, &idx);
            if (fname) {
                if (cmd->input_file) {
                    free(cmd->input_file);
                }
                cmd->input_file = fname;
            }
            continue;
        }

        if (cmd_str[idx] == '>') {
            idx++;
            char *fname = extract_word(cmd_str, &idx);
            if (fname) {
                if (cmd->output_file) {
                    free(cmd->output_file);
                }
                cmd->output_file = fname;
            }
            continue;
        }

        char *word = extract_word(cmd_str, &idx);
        if (word) {
            if (cmd->argc < MAX_ARGS - 1) {
                cmd->argv[cmd->argc++] = word;
            } else {
                free(word);
            }
        }
    }

    cmd->argv[cmd->argc] = NULL;
    return cmd->argc > 0 || cmd->input_file != NULL || cmd->output_file != NULL;
}

int parse_pipeline(const char *input, Pipeline *pl) {
    pl->command_count = 0;
    if (!input) {
        return 0;
    }

    char stages[MAX_PIPE_CMDS][MAX_LINE_LEN];
    int stage_count = 0;

    char current_stage[MAX_LINE_LEN];
    size_t s_idx = 0;
    int in_single = 0;
    int in_double = 0;
    int escaped = 0;

    for (size_t i = 0; input[i] != '\0'; i++) {
        char c = input[i];

        if (escaped) {
            escaped = 0;
            if (s_idx + 1 < sizeof(current_stage)) {
                current_stage[s_idx++] = c;
            }
            continue;
        }

        if (c == '\\') {
            escaped = 1;
            if (s_idx + 1 < sizeof(current_stage)) {
                current_stage[s_idx++] = c;
            }
            continue;
        }

        if (c == '\'') {
            if (!in_double) {
                in_single = !in_single;
            }
            if (s_idx + 1 < sizeof(current_stage)) {
                current_stage[s_idx++] = c;
            }
            continue;
        }

        if (c == '"') {
            if (!in_single) {
                in_double = !in_double;
            }
            if (s_idx + 1 < sizeof(current_stage)) {
                current_stage[s_idx++] = c;
            }
            continue;
        }

        if (c == '|' && !in_single && !in_double) {
            current_stage[s_idx] = '\0';
            if (stage_count < MAX_PIPE_CMDS) {
                snprintf(stages[stage_count++], MAX_LINE_LEN, "%s", current_stage);
            }
            s_idx = 0;
            continue;
        }

        if (s_idx + 1 < sizeof(current_stage)) {
            current_stage[s_idx++] = c;
        }
    }

    current_stage[s_idx] = '\0';
    if (stage_count < MAX_PIPE_CMDS) {
        snprintf(stages[stage_count++], MAX_LINE_LEN, "%s", current_stage);
    }

    for (int i = 0; i < stage_count; i++) {
        if (parse_single_command(stages[i], &pl->commands[pl->command_count])) {
            pl->command_count++;
        }
    }

    return pl->command_count;
}

void pipeline_free(Pipeline *pl) {
    if (!pl) {
        return;
    }
    for (int i = 0; i < pl->command_count; i++) {
        for (int a = 0; a < pl->commands[i].argc; a++) {
            if (pl->commands[i].argv[a]) {
                free(pl->commands[i].argv[a]);
                pl->commands[i].argv[a] = NULL;
            }
        }
        pl->commands[i].argc = 0;
        if (pl->commands[i].input_file) {
            free(pl->commands[i].input_file);
            pl->commands[i].input_file = NULL;
        }
        if (pl->commands[i].output_file) {
            free(pl->commands[i].output_file);
            pl->commands[i].output_file = NULL;
        }
    }
    pl->command_count = 0;
}
