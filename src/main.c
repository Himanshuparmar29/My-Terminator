#include "common.h"
#include "history.h"
#include "gui.h"

ExecutionMode global_exec_mode = MODE_THREAD;

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "useProcess=True") == 0) {
            global_exec_mode = MODE_PROCESS;
        }
    }

    history_init(global_exec_mode);

    GuiApp *app = gui_init(global_exec_mode);
    if (!app) {
        history_cleanup();
        return 1;
    }

    gui_run(app);
    gui_cleanup(app);
    history_cleanup();

    return 0;
}
