#include "common.h"
#include "tab.h"
#include "parse.h"
#include "execute.h"
#include "multiwatch.h"
#include "hotkeys.h"
#include "jobs.h"
#include "history.h"
#include <X11/keysym.h>
#include <assert.h>

ExecutionMode global_exec_mode = MODE_THREAD;

static int g_tests_run = 0;
static int g_tests_passed = 0;

static void assert_test(int condition, const char *desc) {
    g_tests_run++;
    if (condition) {
        g_tests_passed++;
        printf("[PASS] %s\n", desc);
    } else {
        printf("[FAIL] %s\n", desc);
    }
}

static void wait_for_prompt(Tab *tab) {
    char buf[1024];
    int n;
    while ((n = read(tab->from_shell_fd, buf, sizeof(buf) - 1)) > 0) {
        tab_append_output(tab, buf, n);
        if (tab->line_count > 0 && strstr(tab->lines[tab->line_count - 1], "user@terminator:")) {
            break;
        }
    }
}

static void send_cmd(Tab *tab, const char *cmd) {
    dprintf(tab->to_shell_fd, "%s\n", cmd);
    wait_for_prompt(tab);
}

static int tab_has_output(Tab *tab, const char *needle) {
    for (int i = 0; i < tab->line_count; i++) {
        if (tab->lines[i] && strstr(tab->lines[i], needle)) return 1;
    }
    return 0;
}

static void run_expansion_unit_tests(void) {
    printf("\n--- PART 0: Expansion & Quoting Unit Tests ---\n");

    char out[MAX_LINE_LEN];
    const char *home = getenv("HOME");
    assert(home != NULL);

    setenv("TEST_HARNESS_VAR", "harness_val_123", 1);
    parse_expand_variables("echo $TEST_HARNESS_VAR", out, sizeof(out), 0);
    assert_test(strstr(out, "harness_val_123") != NULL, "Environment variable $VAR expands");

    parse_expand_variables("echo ${TEST_HARNESS_VAR}", out, sizeof(out), 0);
    assert_test(strstr(out, "harness_val_123") != NULL, "Braced variable ${VAR} expands");

    parse_expand_variables("echo $?", out, sizeof(out), 42);
    assert_test(strstr(out, "42") != NULL, "Special variable $? expands to last exit status");

    parse_expand_variables("echo 'literal: $TEST_HARNESS_VAR'", out, sizeof(out), 0);
    assert_test(strstr(out, "literal: $TEST_HARNESS_VAR") != NULL, "Single quotes prevent variable expansion");

    parse_expand_variables("echo \"expanded: $TEST_HARNESS_VAR\"", out, sizeof(out), 0);
    assert_test(strstr(out, "expanded: harness_val_123") != NULL, "Double quotes allow variable expansion");

    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) strcpy(cwd, "/tmp");

    parse_expand_substitutions("echo $(echo inside_subst)", out, sizeof(out), cwd, 0);
    assert_test(strstr(out, "inside_subst") != NULL, "Command substitution $(...) evaluated");

    parse_expand_substitutions("echo `echo inside_backtick`", out, sizeof(out), cwd, 0);
    assert_test(strstr(out, "inside_backtick") != NULL, "Backtick substitution evaluated");

    parse_expand_substitutions("echo $(echo outer_$(echo inner))", out, sizeof(out), cwd, 0);
    assert_test(strstr(out, "outer_inner") != NULL, "Nested command substitutions evaluated innermost first");
}

static void run_multiline_unit_tests(void) {
    printf("\n--- PART 1: Multiline & Quoting Continuation Tests ---\n");

    int in_q = 0;
    assert_test(parse_is_incomplete("echo \"hello") == 1, "Unclosed double quote is incomplete");
    assert_test(parse_is_incomplete("echo 'hello") == 1, "Unclosed single quote is incomplete");
    assert_test(parse_is_incomplete("echo hello \\") == 1, "Trailing backslash is incomplete");
    assert_test(parse_is_incomplete("echo $(pwd") == 1, "Unclosed substitution $( is incomplete");
    assert_test(parse_is_incomplete("echo `pwd") == 1, "Unclosed backtick is incomplete");

    assert_test(parse_is_incomplete("echo \"hello\"") == 0, "Closed double quote is complete");
    assert_test(parse_is_incomplete("echo 'hello'") == 0, "Closed single quote is complete");
    assert_test(parse_is_incomplete("echo $(pwd)") == 0, "Closed substitution is complete");

    parse_is_incomplete_state("echo \"Isn't process creation exciting?\\", &in_q);
    assert_test(in_q == 1, "Quote spanning with trailing backslash has in_quotes=1");

    in_q = 0;
    parse_is_incomplete_state("echo \"Files in $(basename $(pwd)): $(ls | \\", &in_q);
    assert_test(in_q == 0, "Substitution with trailing backslash has in_quotes=0");
}

static void run_builtin_command_tests(void) {
    printf("\n--- PART 2: Built-in & External Command Tests ---\n");

    char cwd[1024];
    strcpy(cwd, "/tmp");
    int status = 0;

    execute_command_string("cd /usr", cwd, &status, -1, STDOUT_FILENO);
    assert_test(strcmp(cwd, "/usr") == 0, "cd /usr updates CWD to /usr");

    execute_command_string("cd /nonexistent_harness_dir_dummy404", cwd, &status, -1, STDOUT_FILENO);
    assert_test(status != 0, "cd to non-existent directory returns non-zero status");

    execute_command_string("true", cwd, &status, -1, STDOUT_FILENO);
    assert_test(status == 0, "true builtin returns exit status 0");

    execute_command_string("false", cwd, &status, -1, STDOUT_FILENO);
    assert_test(status != 0, "false builtin returns non-zero status");

    execute_command_string("notacommand_harness_404", cwd, &status, -1, STDOUT_FILENO);
    assert_test(status == 127, "Unknown command returns exit status 127");

    int p[2];
    if (pipe(p) == 0) {
        execute_command_string("clear", cwd, &status, -1, p[1]);
        close(p[1]);
        char cbuf[64];
        int cn = read(p[0], cbuf, sizeof(cbuf) - 1);
        close(p[0]);
        if (cn > 0) cbuf[cn] = '\0'; else cbuf[0] = '\0';
        assert_test(status == 0 && strstr(cbuf, "2J") != NULL, "clear builtin executed with exit status 0 and clear escape code");
    }
}

static void run_redirection_tests(void) {
    printf("\n--- PART 3: Task 4 Redirection Tests ---\n");

    const char *test_out = "/tmp/term_harness_out.txt";
    const char *test_in = "/tmp/term_harness_in.txt";
    unlink(test_out);
    unlink(test_in);

    FILE *f = fopen(test_in, "w");
    assert(f != NULL);
    fprintf(f, "harness_redirection_input_data\n");
    fclose(f);

    int status = 0;
    char cwd[1024] = "/tmp";

    int p[2];
    if (pipe(p) == 0) {
        execute_command_string("echo harness_redirection_output_data > /tmp/term_harness_out.txt", cwd, &status, -1, p[1]);
        close(p[1]);
        close(p[0]);
        assert_test(status == 0, "Output redirection executed cleanly");
    }

    FILE *fin = fopen(test_out, "r");
    assert_test(fin != NULL, "Output redirection created target file");
    if (fin) {
        char buf[256];
        if (fgets(buf, sizeof(buf), fin)) {
            assert_test(strstr(buf, "harness_redirection_output_data") != NULL, "Output redirection file content verified");
        }
        fclose(fin);
    }

    if (pipe(p) == 0) {
        char buf[256];
        execute_command_string("cat < /tmp/term_harness_in.txt", cwd, &status, -1, p[1]);
        close(p[1]);
        int n = read(p[0], buf, sizeof(buf) - 1);
        close(p[0]);
        if (n > 0) buf[n] = '\0'; else buf[0] = '\0';
        assert_test(status == 0 && strstr(buf, "harness_redirection_input_data") != NULL, "Input redirection successfully read file contents");
    }

    if (pipe(p) == 0) {
        execute_command_string("cat < /tmp/term_harness_nonexistent_123.txt", cwd, &status, -1, p[1]);
        close(p[1]);
        close(p[0]);
        assert_test(status != 0, "Missing input file produces error and non-zero exit status");
    }

    unlink(test_out);
    unlink(test_in);
}

static void run_pipeline_tests(void) {
    printf("\n--- PART 4: Task 5 Pipeline Tests ---\n");

    int status = 0;
    char cwd[1024] = "/tmp";

    int p[2];
    if (pipe(p) == 0) {
        execute_command_string("printf \"apple\\nbanana\\ncherry\\n\" | grep banana", cwd, &status, -1, p[1]);
        close(p[1]);
        char buf[256];
        int n = read(p[0], buf, sizeof(buf) - 1);
        close(p[0]);
        if (n > 0) buf[n] = '\0'; else buf[0] = '\0';
        assert_test(status == 0, "Pipeline executed with exit status 0");
        assert_test(strstr(buf, "banana") != NULL, "Pipeline grep captured expected output");
    }

    if (pipe(p) == 0) {
        execute_command_string("printf \"one\\ntwo\\nthree\\n\" | wc -l", cwd, &status, -1, p[1]);
        close(p[1]);
        char buf[256];
        int n = read(p[0], buf, sizeof(buf) - 1);
        close(p[0]);
        if (n > 0) buf[n] = '\0'; else buf[0] = '\0';
        assert_test(atoi(buf) == 3, "3-stage pipeline wc -l counted lines correctly");
    }
}

static void run_multiwatch_tests(void) {
    printf("\n--- PART 5: Task 6 multiWatch Tests ---\n");

    assert_test(multiwatch_is_command("multiWatch [\"date +%T\", \"ls | wc -l\"]") == 1, "multiWatch command detection");
    assert_test(multiwatch_is_command("echo multiWatch") == 0, "Non-multiWatch command correctly rejected");
}

static void run_hotkeys_tests(void) {
    printf("\n--- PART 6: Task 7 Hotkeys Tests ---\n");

    HotkeyBinding bindings[HOTKEY_COUNT];
    hotkeys_init(bindings);

    assert_test(bindings[HOTKEY_MOVE_START].keysym == XK_a, "Default move_start is Ctrl+A");
    assert_test(bindings[HOTKEY_MOVE_END].keysym == XK_e, "Default move_end is Ctrl+E");
    assert_test(bindings[HOTKEY_DEL_TO_START].keysym == XK_u, "Default del_to_start is Ctrl+U");
    assert_test(bindings[HOTKEY_DEL_TO_END].keysym == XK_k, "Default del_to_end is Ctrl+K");
    assert_test(bindings[HOTKEY_DEL_PREV_WORD].keysym == XK_w, "Default del_prev_word is Ctrl+W");
    assert_test(bindings[HOTKEY_CLEAR_SCREEN].keysym == XK_l, "Default clear_screen is Ctrl+L");
    assert_test(bindings[HOTKEY_EXIT_SHELL].keysym == XK_d, "Default exit_shell is Ctrl+D");
    assert_test(bindings[HOTKEY_COPY].keysym == XK_c, "Default copy is Ctrl+Shift+C");
    assert_test(bindings[HOTKEY_PASTE].keysym == XK_v, "Default paste is Ctrl+Shift+V");
    assert_test(bindings[HOTKEY_SUSPEND].keysym == XK_z, "Default suspend is Ctrl+Z");
    assert_test(bindings[HOTKEY_HISTORY_PREV].keysym == XK_Up, "Default history_prev is Up");
    assert_test(bindings[HOTKEY_HISTORY_NEXT].keysym == XK_Down, "Default history_next is Down");

    int act = hotkeys_match(bindings, XK_a, ControlMask);
    assert_test(act == HOTKEY_MOVE_START, "Matched Ctrl+A to HOTKEY_MOVE_START");

    act = hotkeys_match(bindings, XK_c, ControlMask | ShiftMask);
    assert_test(act == HOTKEY_COPY, "Matched Ctrl+Shift+C to HOTKEY_COPY");

    act = hotkeys_match(bindings, XK_c, ControlMask);
    assert_test(act != HOTKEY_COPY, "Ctrl+C does not match HOTKEY_COPY");

    act = hotkeys_match(bindings, XK_v, ControlMask | ShiftMask);
    assert_test(act == HOTKEY_PASTE, "Matched Ctrl+Shift+V to HOTKEY_PASTE");

    act = hotkeys_match(bindings, XK_v, ControlMask);
    assert_test(act != HOTKEY_PASTE, "Ctrl+V alone does not match HOTKEY_PASTE");
}

static void run_jobs_tests(void) {
    printf("\n--- PART 7: Task 8 Signals & Job Control Tests ---\n");

    jobs_init();

    pid_t dummy_pids[1] = { 88888 };
    int jid = jobs_add(88888, dummy_pids, 1, "sleep 100", JOB_STOPPED);
    assert_test(jid > 0, "Job added successfully to jobs table");

    Job *j = jobs_get(jid);
    assert_test(j != NULL && j->state == JOB_STOPPED, "Retrieved stopped job by id");
    assert_test(j != NULL && strcmp(j->cmd, "sleep 100") == 0, "Job command string matches");
}

static void run_history_tests(void) {
    printf("\n--- PART 8: Task 9 Searchable History Tests ---\n");

    history_add("harness_cmd_1");
    history_add("harness_cmd_2");
    history_add("harness_cmd_3");
    assert_test(history_get_count() >= 3, "History entries recorded");

    char entry[MAX_LINE_LEN];
    assert_test(history_get_entry(history_get_count() - 1, entry, sizeof(entry)) == 1, "Retrieved last history entry");
    assert_test(strcmp(entry, "harness_cmd_3") == 0, "History entry matches added command");

    history_add("./myprog < in.txt");
    history_add("gcc -o myprog myprog.c");
    history_add("harness_exact_target");
    history_add("other_command");
    history_add("harness_exact_target");

    char res[10][MAX_LINE_LEN];
    int n = history_search("harness_exact_target", res, 10);
    assert_test(n == 1 && strcmp(res[0], "harness_exact_target") == 0, "Rule a: Exact match returns most recent entry");

    n = history_search("myprog", res, 10);
    assert_test(n == 2, "Rule b: Substring search matches all commands sharing longest common substring");
    int f1 = (strcmp(res[0], "./myprog < in.txt") == 0 || strcmp(res[1], "./myprog < in.txt") == 0);
    int f2 = (strcmp(res[0], "gcc -o myprog myprog.c") == 0 || strcmp(res[1], "gcc -o myprog myprog.c") == 0);
    assert_test(f1 && f2, "Both matching commands retrieved for search term myprog");

    n = history_search("xyz", res, 10);
    assert_test(n == 0, "Rule c: Unmatched term returns 0 results for no match");

    history_add("cargo test");
    history_add("npm test");
    history_add("cargo test");
    n = history_search("cargo test", res, 10);
    assert_test(n == 1, "Exact search deduplicates identical entries");

    int pfd[2];
    if (pipe(pfd) == 0) {
        history_print(pfd[1]);
        close(pfd[1]);
        char hbuf[4096];
        int bytes = read(pfd[0], hbuf, sizeof(hbuf) - 1);
        close(pfd[0]);
        if (bytes > 0) hbuf[bytes] = '\0'; else hbuf[0] = '\0';
        int lines = 0;
        for (int i = 0; i < bytes; i++) if (hbuf[i] == '\n') lines++;
        assert_test(lines <= 1000, "History print outputs at most 1000 commands");
    }
}

static void run_execution_modes_tests(ExecutionMode mode) {
    printf("\n--- PART 9: %s Mode Tab Isolation & History Tests ---\n", mode == MODE_PROCESS ? "Process" : "Thread");

    global_exec_mode = mode;
    history_init(mode);

    Tab *tab1 = tab_create(1, "/usr", mode);
    Tab *tab2 = tab_create(2, "/etc", mode);
    assert(tab1 != NULL);
    assert(tab2 != NULL);

    wait_for_prompt(tab1);
    wait_for_prompt(tab2);

    send_cmd(tab1, "cd /tmp");
    send_cmd(tab2, "echo tab2_ready");

    assert_test(strcmp(tab1->cwd, "/tmp") == 0, "Tab 1 CWD changed to /tmp");
    assert_test(strcmp(tab2->cwd, "/etc") == 0, "Tab 2 CWD preserved as /etc");

    send_cmd(tab1, "echo shared_harness_cmd");
    send_cmd(tab2, "history");

    assert_test(tab_has_output(tab2, "shared_harness_cmd"), "Shared history accessible across tabs");

    tab_destroy(tab1);
    send_cmd(tab2, "echo tab2_alive");
    assert_test(tab_has_output(tab2, "tab2_alive"), "Tab 2 survived destruction of sibling tab");

    tab_destroy(tab2);
    history_cleanup();
}

int main(void) {
    printf("==========================================================\n");
    printf("         Terminator Project Comprehensive Test Harness    \n");
    printf("==========================================================\n");

    unlink("/tmp/harness_test_histfile.txt");
    setenv("HISTFILE", "/tmp/harness_test_histfile.txt", 1);

    history_init(MODE_THREAD);

    run_expansion_unit_tests();
    run_multiline_unit_tests();
    run_builtin_command_tests();
    run_redirection_tests();
    run_pipeline_tests();
    run_multiwatch_tests();
    run_hotkeys_tests();
    run_jobs_tests();
    run_history_tests();

    history_cleanup();

    run_execution_modes_tests(MODE_THREAD);
    run_execution_modes_tests(MODE_PROCESS);

    printf("\n==========================================================\n");
    printf("Harness Results: %d / %d tests passed\n", g_tests_passed, g_tests_run);
    printf("==========================================================\n");

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
