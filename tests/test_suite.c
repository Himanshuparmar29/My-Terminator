#include "common.h"
#include "parse.h"
#include "execute.h"
#include "multiwatch.h"
#include "history.h"
#include "hotkeys.h"
#include "jobs.h"
#include <assert.h>

ExecutionMode global_exec_mode = MODE_THREAD;

static int test_count = 0;
static int pass_count = 0;

static void assert_test(int condition, const char *name) {
    test_count++;
    if (condition) {
        pass_count++;
        printf("[PASS] %s\n", name);
    } else {
        printf("[FAIL] %s\n", name);
    }
}

static void test_multiline_incomplete(void) {
    printf("\n--- Testing Task 3: Multiline Detection ---\n");
    assert_test(parse_is_incomplete("echo \"hello") == 1, "Unclosed double quote is incomplete");
    assert_test(parse_is_incomplete("echo 'hello") == 1, "Unclosed single quote is incomplete");
    assert_test(parse_is_incomplete("echo one \\") == 1, "Trailing backslash is incomplete");
    assert_test(parse_is_incomplete("echo $(pwd") == 1, "Unclosed substitution is incomplete");
    assert_test(parse_is_incomplete("echo `pwd") == 1, "Unclosed backtick is incomplete");
    assert_test(parse_is_incomplete("echo \"hello world\"") == 0, "Closed double quote is complete");
    assert_test(parse_is_incomplete("echo 'literal string'") == 0, "Closed single quote is complete");
    assert_test(parse_is_incomplete("echo $(pwd)") == 0, "Closed substitution is complete");
    assert_test(parse_is_incomplete("echo `pwd`") == 0, "Closed backtick is complete");
    assert_test(parse_is_incomplete("echo hello") == 0, "Plain command is complete");

    int in_q = 0;
    assert_test(parse_is_incomplete_state("echo \"Isn't process creation exciting?\\", &in_q) == 1 && in_q == 1, "Quote spanning with trailing backslash has in_quotes=1");
    assert_test(parse_is_incomplete_state("echo \"Files in $(basename $(pwd)): $(ls | \\", &in_q) == 1 && in_q == 0, "Substitution with trailing backslash has in_quotes=0");
    assert_test(parse_is_incomplete_state("echo 'literal: $HOME' \"expanded: $HOME\"", &in_q) == 0, "Complete quoted command has incomplete=0");
}

static void test_variable_expansion(void) {
    printf("\n--- Testing Task 3: Variable and Quoting Expansion ---\n");
    char out[MAX_LINE_LEN];

    parse_expand_variables("echo 'literal: $HOME'", out, sizeof(out), 0);
    assert_test(strcmp(out, "echo 'literal: $HOME'") == 0, "Single quotes prevent $HOME expansion");

    setenv("MY_TEST_VAR", "Terminator123", 1);
    parse_expand_variables("echo \"$MY_TEST_VAR\"", out, sizeof(out), 0);
    assert_test(strcmp(out, "echo \"Terminator123\"") == 0, "Double quotes expand $VAR");

    parse_expand_variables("echo $?", out, sizeof(out), 42);
    assert_test(strcmp(out, "echo 42") == 0, "$? expands to last exit status");

    parse_expand_variables("echo ${MY_TEST_VAR}_ok", out, sizeof(out), 0);
    assert_test(strcmp(out, "echo Terminator123_ok") == 0, "${VAR} expands correctly");
}

static void test_command_substitution(void) {
    printf("\n--- Testing Task 3: Command Substitution ---\n");
    char out[MAX_LINE_LEN];
    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) {
        strcpy(cwd, "/tmp");
    }

    parse_expand_substitutions("echo $(echo inside)", out, sizeof(out), cwd, 0);
    assert_test(strcmp(out, "echo inside") == 0, "Simple $(echo inside) substitution");

    parse_expand_substitutions("echo `echo backtick`", out, sizeof(out), cwd, 0);
    assert_test(strcmp(out, "echo backtick") == 0, "Backtick substitution");

    parse_expand_substitutions("echo $(echo outer $(echo inner))", out, sizeof(out), cwd, 0);
    assert_test(strcmp(out, "echo outer inner") == 0, "Nested $(...) evaluated innermost first");

    parse_expand_substitutions("echo \"Files in $(basename $(pwd)): $(echo 3)\"", out, sizeof(out), "/home/user/mydir", 0);
    assert_test(strcmp(out, "echo \"Files in mydir: 3\"") == 0, "Canonical multiline substitution expansion matches");
}

static void test_redirection(void) {
    printf("\n--- Testing Task 4: I/O Redirection ---\n");
    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) {
        strcpy(cwd, "/tmp");
    }
    int status = 0;

    int p[2];
    if (pipe(p) != 0) return;

    execute_command_string("echo \"redirection test\" > /tmp/term_redir_test.txt", cwd, &status, -1, p[1]);
    assert_test(status == 0, "Output redirection executed with exit status 0");

    char buf[256];
    execute_command_string("cat < /tmp/term_redir_test.txt", cwd, &status, -1, p[1]);
    close(p[1]);
    int n = read(p[0], buf, sizeof(buf) - 1);
    close(p[0]);
    if (n > 0) {
        buf[n] = '\0';
    } else {
        buf[0] = '\0';
    }
    assert_test(strstr(buf, "redirection test") != NULL, "Input redirection successfully read file contents");

    if (pipe(p) != 0) return;
    execute_command_string("cat < /tmp/term_nonexistent_12345.txt", cwd, &status, -1, p[1]);
    close(p[1]);
    close(p[0]);
    assert_test(status == 1, "Missing input file produces error and non-zero exit status");

    unlink("/tmp/term_redir_test.txt");
}

static void test_pipeline(void) {
    printf("\n--- Testing Task 5: Pipelines ---\n");
    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) {
        strcpy(cwd, "/tmp");
    }
    int status = 0;

    int p[2];
    if (pipe(p) != 0) return;
    execute_command_string("printf \"apple\\nbanana\\ncherry\\n\" | grep banana", cwd, &status, -1, p[1]);
    close(p[1]);

    char buf[256];
    int n = read(p[0], buf, sizeof(buf) - 1);
    close(p[0]);
    if (n > 0) {
        buf[n] = '\0';
    } else {
        buf[0] = '\0';
    }

    assert_test(status == 0, "Pipeline executed with exit status 0");
    assert_test(strstr(buf, "banana") != NULL, "Pipeline grep captured expected output");

    if (pipe(p) != 0) return;
    execute_command_string("printf \"one\\ntwo\\nthree\\n\" | wc -l", cwd, &status, -1, p[1]);
    close(p[1]);
    n = read(p[0], buf, sizeof(buf) - 1);
    close(p[0]);
    if (n > 0) {
        buf[n] = '\0';
    } else {
        buf[0] = '\0';
    }
    int count = atoi(buf);
    assert_test(count == 3, "Pipeline wc -l counted 3 lines correctly");
}

static void test_builtins_and_status(void) {
    printf("\n--- Testing Task 2: Built-ins and Exit Status ---\n");
    char cwd[1024];
    strcpy(cwd, "/usr");
    int status = 0;

    execute_command_string("cd /tmp", cwd, &status, -1, STDOUT_FILENO);
    assert_test(status == 0 && strcmp(cwd, "/tmp") == 0, "cd /tmp updated cwd to /tmp");

    execute_command_string("cd /nonexistent_directory_9876", cwd, &status, -1, STDOUT_FILENO);
    assert_test(status == 1, "cd to non-existent directory returned status 1");

    execute_command_string("false", cwd, &status, -1, STDOUT_FILENO);
    assert_test(status != 0, "false command preserves non-zero status in $?");

    execute_command_string("true", cwd, &status, -1, STDOUT_FILENO);
    assert_test(status == 0, "true command returns status 0");

    execute_command_string("this_command_does_not_exist_404", cwd, &status, -1, STDOUT_FILENO);
    assert_test(status == 127, "Unknown command returns exit status 127");

    int clear_p[2];
    if (pipe(clear_p) == 0) {
        execute_command_string("clear", cwd, &status, -1, clear_p[1]);
        close(clear_p[1]);
        char cbuf[64];
        int cn = read(clear_p[0], cbuf, sizeof(cbuf) - 1);
        close(clear_p[0]);
        if (cn > 0) {
            cbuf[cn] = '\0';
        } else {
            cbuf[0] = '\0';
        }
        assert_test(status == 0 && strstr(cbuf, "2J") != NULL, "clear builtin executed with exit status 0 and clear escape code");
    }
}

static void test_multiwatch_syntax_and_cancel(void) {
    printf("\n--- Testing Task 6: multiWatch ---\n");
    assert_test(multiwatch_is_command("multiWatch [\"date +%T\", \"ls | wc -l\"]") == 1, "multiWatch command detection");
    assert_test(multiwatch_is_command("echo multiWatch") == 0, "Non-multiWatch command not matched");

    int cancel_pipe[2];
    int out_pipe[2];
    if (pipe(cancel_pipe) != 0 || pipe(out_pipe) != 0) return;

    if (write(cancel_pipe[1], "\x03\n", 2) < 0) return;

    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) {
        strcpy(cwd, "/tmp");
    }

    int ret = multiwatch_execute("multiWatch [\"echo hello\", \"echo world\"]", cwd, cancel_pipe[0], out_pipe[1]);
    close(cancel_pipe[1]);
    close(cancel_pipe[0]);
    close(out_pipe[1]);

    char buf[1024];
    int n = read(out_pipe[0], buf, sizeof(buf) - 1);
    close(out_pipe[0]);
    if (n > 0) {
        buf[n] = '\0';
    } else {
        buf[0] = '\0';
    }

    assert_test(ret == 0, "multiWatch exited cleanly on Ctrl+C");
    assert_test(strstr(buf, "^C") != NULL, "multiWatch emitted ^C on cancellation");
}

static void test_history(void) {
    printf("\n--- Testing Task 9: Searchable History ---\n");
    history_add("test_cmd_1");
    history_add("test_cmd_2");
    history_add("test_cmd_3");

    assert_test(history_get_count() >= 3, "History entries recorded");

    char entry[MAX_LINE_LEN];
    assert_test(history_get_entry(history_get_count() - 1, entry, sizeof(entry)) == 1, "Retrieved last history entry");
    assert_test(strcmp(entry, "test_cmd_3") == 0, "History entry matches added command");

    history_add("./myprog < in.txt");
    history_add("gcc -o myprog myprog.c");
    history_add("exact_target");
    history_add("intermediate_cmd");
    history_add("exact_target");

    char res[10][MAX_LINE_LEN];
    int n = history_search("exact_target", res, 10);
    assert_test(n == 1 && strcmp(res[0], "exact_target") == 0, "Exact match returns most recent entry");

    n = history_search("myprog", res, 10);
    assert_test(n == 2, "Substring search matches all commands sharing longest common substring");
    int found1 = (strcmp(res[0], "./myprog < in.txt") == 0 || strcmp(res[1], "./myprog < in.txt") == 0);
    int found2 = (strcmp(res[0], "gcc -o myprog myprog.c") == 0 || strcmp(res[1], "gcc -o myprog myprog.c") == 0);
    assert_test(found1 && found2, "Both matching commands retrieved for search term myprog");

    n = history_search("xyz", res, 10);
    assert_test(n == 0, "Unmatched term returns 0 results for no match");

    int pfd[2];
    if (pipe(pfd) == 0) {
        history_print(pfd[1]);
        close(pfd[1]);
        char hbuf[4096];
        int bytes = read(pfd[0], hbuf, sizeof(hbuf) - 1);
        close(pfd[0]);
        if (bytes > 0) hbuf[bytes] = '\0';
        else hbuf[0] = '\0';
        int lines = 0;
        for (int i = 0; i < bytes; i++) if (hbuf[i] == '\n') lines++;
        assert_test(lines <= 1000, "history command outputs at most 1000 commands");
    }
}

static void test_hotkeys(void) {
    printf("\n--- Testing Task 7: Hotkeys and Custom Config ---\n");
    HotkeyBinding bindings[HOTKEY_COUNT];
    hotkeys_init(bindings);

    assert_test(bindings[HOTKEY_MOVE_START].keysym == XK_a, "Default move_start is Ctrl+A");
    assert_test(bindings[HOTKEY_MOVE_END].keysym == XK_e, "Default move_end is Ctrl+E");
    assert_test(bindings[HOTKEY_HISTORY_PREV].keysym == XK_Up, "Default history_prev is Up");
    assert_test(bindings[HOTKEY_HISTORY_NEXT].keysym == XK_Down, "Default history_next is Down");
    assert_test(bindings[HOTKEY_DEL_TO_START].keysym == XK_u, "Default del_to_start is Ctrl+U");
    assert_test(bindings[HOTKEY_DEL_TO_END].keysym == XK_k, "Default del_to_end is Ctrl+K");
    assert_test(bindings[HOTKEY_DEL_PREV_WORD].keysym == XK_w, "Default del_prev_word is Ctrl+W");
    assert_test(bindings[HOTKEY_CLEAR_SCREEN].keysym == XK_l, "Default clear_screen is Ctrl+L");
    assert_test(bindings[HOTKEY_EXIT_SHELL].keysym == XK_d, "Default exit_shell is Ctrl+D");
    assert_test(bindings[HOTKEY_COPY].keysym == XK_c, "Default copy is Ctrl+Shift+C");
    assert_test(bindings[HOTKEY_PASTE].keysym == XK_v, "Default paste is Ctrl+Shift+V");
    assert_test(bindings[HOTKEY_SUSPEND].keysym == XK_z, "Default suspend is Ctrl+Z");

    assert_test(hotkeys_match(bindings, XK_a, ControlMask) == HOTKEY_MOVE_START, "Matched Ctrl+A to HOTKEY_MOVE_START");
    assert_test(hotkeys_match(bindings, XK_A, ControlMask) == HOTKEY_MOVE_START, "Matched Ctrl+A (capital) to HOTKEY_MOVE_START");
    assert_test(hotkeys_match(bindings, XK_c, ControlMask | ShiftMask) == HOTKEY_COPY, "Matched Ctrl+Shift+C to HOTKEY_COPY");
    assert_test(hotkeys_match(bindings, XK_c, ControlMask) == -1, "Ctrl+C does not match HOTKEY_COPY");
    assert_test(hotkeys_match(bindings, XK_v, ControlMask | ShiftMask) == HOTKEY_PASTE, "Matched Ctrl+Shift+V to HOTKEY_PASTE");
    assert_test(hotkeys_match(bindings, XK_V, ControlMask | ShiftMask) == HOTKEY_PASTE, "Matched Ctrl+Shift+V (capital) to HOTKEY_PASTE");
    assert_test(hotkeys_match(bindings, XK_v, ControlMask) == -1, "Ctrl+V alone does not match HOTKEY_PASTE");
    assert_test(hotkeys_match(bindings, XK_z, ControlMask) == HOTKEY_SUSPEND, "Matched Ctrl+Z to HOTKEY_SUSPEND");
    assert_test(hotkeys_match(bindings, XK_Up, 0) == HOTKEY_HISTORY_PREV, "Matched Up arrow to HOTKEY_HISTORY_PREV");
}

static void test_job_control(void) {
    printf("\n--- Testing Task 8: Signals and Job Control ---\n");
    jobs_init();

    pid_t dummy_pids[1] = { 99999 };
    int jid = jobs_add(99999, dummy_pids, 1, "sleep 100", JOB_STOPPED);
    assert_test(jid > 0, "Job added successfully to jobs table");

    Job *j = jobs_get(jid);
    assert_test(j != NULL && j->state == JOB_STOPPED, "Retrieved stopped job by id");
    assert_test(strcmp(j->cmd, "sleep 100") == 0, "Job command string matches");

    int p[2];
    if (pipe(p) != 0) return;

    int status = 0;
    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) {
        strcpy(cwd, "/tmp");
    }

    execute_command_string("sleep 0.1 &", cwd, &status, -1, p[1]);
    assert_test(status == 0, "Background command '&' returns status 0 immediately");

    Job *last_job = jobs_get_last();
    assert_test(last_job != NULL && last_job->state == JOB_RUNNING, "Background job recorded in Running state");

    jobs_print(p[1]);
    close(p[1]);

    char buf[1024];
    int n = read(p[0], buf, sizeof(buf) - 1);
    close(p[0]);
    if (n > 0) {
        buf[n] = '\0';
    } else {
        buf[0] = '\0';
    }
    assert_test(strstr(buf, "Running") != NULL || strstr(buf, "Done") != NULL, "jobs command prints job list");
}

int main(void) {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
    unlink("/tmp/test_suite_histfile.txt");
    setenv("HISTFILE", "/tmp/test_suite_histfile.txt", 1);
    history_init(MODE_THREAD);

    test_multiline_incomplete();
    test_variable_expansion();
    test_command_substitution();
    test_redirection();
    test_pipeline();
    test_builtins_and_status();
    test_multiwatch_syntax_and_cancel();
    test_history();
    test_hotkeys();
    test_job_control();

    history_cleanup();

    printf("\n=========================================\n");
    printf("Test Results: %d / %d tests passed\n", pass_count, test_count);
    printf("=========================================\n");

    return (pass_count == test_count) ? 0 : 1;
}
