#include "common.h"
#include "tab.h"
#include "history.h"
#include <assert.h>

ExecutionMode global_exec_mode = MODE_THREAD;

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

static void test_mode_execution(ExecutionMode mode) {
    global_exec_mode = mode;
    printf("\n--- Testing %s Mode ---\n", mode == MODE_PROCESS ? "Process" : "Thread");

    history_init(mode);

    Tab *tab1 = tab_create(1, "/usr", mode);
    Tab *tab2 = tab_create(2, "/etc", mode);
    assert(tab1 != NULL);
    assert(tab2 != NULL);

    wait_for_prompt(tab1);
    wait_for_prompt(tab2);

    dprintf(tab1->to_shell_fd, "cd /tmp\n");
    wait_for_prompt(tab1);

    dprintf(tab2->to_shell_fd, "echo in_tab_2\n");
    wait_for_prompt(tab2);

    printf("Tab 1 CWD: %s\n", tab1->cwd);
    printf("Tab 2 CWD: %s\n", tab2->cwd);

    assert(strcmp(tab1->cwd, "/tmp") == 0);
    assert(strcmp(tab2->cwd, "/etc") == 0);
    printf("[PASS] Independent CWD verified in %s mode\n", mode == MODE_PROCESS ? "Process" : "Thread");

    dprintf(tab1->to_shell_fd, "echo cmd_from_tab_1\n");
    wait_for_prompt(tab1);

    dprintf(tab2->to_shell_fd, "history\n");
    char buf[1024];
    int n = 0;
    int found_cmd1 = 0;
    while ((n = read(tab2->from_shell_fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        tab_append_output(tab2, buf, n);
        if (strstr(buf, "cmd_from_tab_1")) {
            found_cmd1 = 1;
        }
        if (tab2->line_count > 0 && strstr(tab2->lines[tab2->line_count - 1], "user@terminator:")) {
            break;
        }
    }

    assert(found_cmd1 == 1);
    printf("[PASS] Shared history verified across tabs in %s mode\n", mode == MODE_PROCESS ? "Process" : "Thread");

    tab_destroy(tab1);

    dprintf(tab2->to_shell_fd, "echo still_alive\n");
    int found_alive = 0;
    while ((n = read(tab2->from_shell_fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        tab_append_output(tab2, buf, n);
        if (strstr(buf, "still_alive")) {
            found_alive = 1;
        }
        if (tab2->line_count > 0 && strstr(tab2->lines[tab2->line_count - 1], "user@terminator:")) {
            break;
        }
    }
    assert(found_alive == 1);
    printf("[PASS] Tab survival after sibling destruction verified in %s mode\n", mode == MODE_PROCESS ? "Process" : "Thread");

    tab_destroy(tab2);
    history_cleanup();
}

int main(void) {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    test_mode_execution(MODE_THREAD);
    test_mode_execution(MODE_PROCESS);

    printf("\n=========================================\n");
    printf("All Mode Tests Passed Successfully!\n");
    printf("=========================================\n");
    return 0;
}
