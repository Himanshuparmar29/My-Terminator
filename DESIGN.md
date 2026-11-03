# Terminator Design Document

## 1. System Architecture

Terminator is designed around a decoupled client-server pattern where the **GUI frontend** (X11 event loop and window rendering) communicates with independent **Tab Shell backends** through unidirectional POSIX pipe channels.

```
+--------------------------------------------------------------------+
|                         X11 Display Server                         |
+---------------------------------+----------------------------------+
                                  | KeyPress, ButtonPress, Expose
                                  v
+--------------------------------------------------------------------+
|                      Terminator GUI (Main Loop)                    |
|  - poll() on ConnectionNumber(dpy) + tab[0..N]->from_shell_fd      |
|  - Renders Active Tab Buffer, Tab Bar, Cursor                      |
+---------------------+------------------------+---------------------+
                      |                        |
         [to_shell]   | [from_shell]           | [to_shell]   | [from_shell]
         pipe         | pipe                   pipe         | pipe
                      v                                     v
+-----------------------------+       +-----------------------------+
|        Tab 1 Shell          |       |        Tab 2 Shell          |
|  - cwd: /tmp                |       |  - cwd: /home               |
|  - last_status: 0           |       |  - last_status: 0           |
|  - Pipeline Executor        |       |  - Pipeline Executor        |
|  - multiWatch Controller    |       |  - multiWatch Controller    |
+-----------------------------+       +-----------------------------+
              |                                     |
              +------------------+------------------+
                                 |
                                 v
               +-----------------------------------+
               |        Shared Command History     |
               | - Thread Mode: pthread_mutex_t    |
               | - Process Mode: sem_t + mmap()    |
               +-----------------------------------+
```

---

## 2. GUI Architecture and Non-Blocking Event Loop

### X11 Integration & Font Sizing
Terminator initializes an X11 connection using `XOpenDisplay()`. It queries a sequence of candidate fonts starting with large 28px/24px bold fonts:
```c
const char *names[] = {
    "-*-fixed-bold-r-*-*-28-*-*-*-*-*-*-*",
    "-misc-fixed-bold-r-normal--24-*-*-*-*-*-*-*",
    "12x24",
    "10x20",
    "fixed",
    NULL
};
```
Character cell metrics (`font_width`, `font_ascent`, `font_descent`, `line_height`) are computed dynamically from `XFontStruct`, allowing all rendering geometry, text placement, and cursor bounds to adapt cleanly to the loaded font.

### Unified `poll()` Sleeper
To avoid 100% CPU busy-waiting, Terminator unifies GUI events and tab output into a single `poll()` call:
1. `fds[0].fd = ConnectionNumber(dpy)` (monitored for `POLLIN`)
2. `fds[1..N].fd = tabs[i]->from_shell_fd` (monitored for `POLLIN | POLLHUP`)
3. `XFlush(dpy)` is called immediately before `poll()` to flush pending draw requests.
4. When `poll()` wakes:
   - If `XPending(dpy) > 0`, events are popped with `XNextEvent()` and dispatched.
   - If a tab pipe is ready, output is read non-blockingly and appended to the tab buffer.
   - If a tab pipe reports EOF/hangup, the tab is cleanly cleaned up.

---

## 3. Tab Management and Text Buffering

Each tab is encapsulated in a `Tab` struct containing:
- Independent working directory buffer: `char cwd[1024];`
- Dynamic scrollback line array: `char **lines;` (up to 10,000 lines)
- Active input line buffer: `char input_line[MAX_LINE_LEN];`
- Cursor offset and scrollback viewport offset: `cursor_pos`, `scroll_offset`
- Multiline input accumulator: `multiline_accum`
- IPC pipes to and from its shell backend.

### CWD Notification via OSC Escape Sequence
When `cd` changes a tab's directory, the tab's shell backend transmits an Operating System Command (OSC) string:
`\033]0;CWD:<path>\007`
The GUI's character stream processor parses this sequence, updates `tab->cwd`, and updates the tab bar label (`[ 1: ~/dir x ]`) without printing the control bytes onto the terminal screen.

---

## 4. Shell Parsing & Multiline Input (Task 3)

The command parser is implemented using finite state machines without relying on `strtok()`, ensuring complete handling of quotes, escapes, and nesting:

### Multiline Incomplete Detection
Before executing an entered command line, `parse_is_incomplete()` scans the text:
- Single quotes `'...'`: Characters inside are literal. Stays open until a matching `'`.
- Double quotes `"..."`: Expands variables and substitutions. Stays open until matching `"`.
- Escapes `\`: Outside single quotes, escapes the next character. A trailing `\` flags an incomplete line.
- Command substitutions: Counts parentheses depth for `$(` and toggles state for backticks.
If any state remains open at end of line, the GUI appends the line, shows continuation prompt `> `, and collects further input.

### Nested Substitution Evaluation
Substitutions (`$(cmd)` and `` `cmd` ``) are resolved from the innermost level outward:
1. `find_innermost_subst()` locates the deepest substitution containing no nested `$(` or `` ` ``.
2. The inner command string is executed through `execute_capture()`, which redirects standard output into a temporary pipe.
3. Trailing newlines are trimmed from the captured text (matching standard POSIX shell behavior).
4. The substitution substring in the outer command is replaced with the captured string.
5. Steps 1–4 repeat until all substitutions are resolved.

---

## 5. Pipeline and Redirection Execution (Tasks 4 & 5)

The execution engine (`src/execute.c`) handles arbitrary pipelines and file redirections using standard POSIX primitives:

1. **Pipeline Construction**:
   For $N$ commands, $N-1$ pipes are allocated via `pipe()`.
2. **Process Spawning**:
   $N$ children are spawned with `fork()`.
3. **Child Descriptor Wiring**:
   - For child $0$: If input redirection (`< input.txt`) is specified, `open(O_RDONLY)` is performed. If the file cannot be opened, an error message is printed to standard error and the child immediately exits with code `1`.
   - For intermediate children: Standard input is connected to `pipe_fds[i-1][0]` and standard output to `pipe_fds[i][1]`.
   - For child $N-1$: Standard output is wired to the output file if `> output.txt` is present, or to the GUI pipe.
   - Crucially, all unused pipe descriptors are closed in both the parent shell and each child process, ensuring EOF is cleanly propagated across the pipeline.
4. **Directory Isolation**:
   In every child process, `chdir(cwd)` is called immediately after `fork()`. This guarantees that directory operations and relative path lookups are completely isolated to the current tab without mutating the process-wide working directory of other threads.

---

## 6. multiWatch Implementation (Task 6)

`multiWatch ["cmd1", "cmd2", ...]` executes multiple commands concurrently:
- Parses quoted command strings from the bracketed array syntax.
- Each watched command is launched as a process group leader (`setpgid(0, 0)`) with its stdout/stderr connected to a dedicated pipe.
- A `poll()` loop monitors all active pipe read ends alongside the GUI cancellation descriptor (`cancel_fd`).
- When output arrives on any pipe, it is written to the terminal with the exact required header and separator:
  ```text
  "cmd_string", <unix_timestamp>:
  ----------------------------------------------------
  <command output>
  ----------------------------------------------------
  ```
- When a command process terminates, it is reaped via `waitpid(..., WNOHANG)` and scheduled to restart after approximately 2 seconds (`finish_time + 2`).
- **Ctrl+C Cancellation**: When the user presses `Ctrl+C` in the active tab, the GUI writes `\x03` to the tab pipe. `multiWatch` detects this on `cancel_fd`, sends `SIGINT` (followed by `SIGKILL` if needed) to all watched process groups, prints `^C`, cleans up all open pipes, and returns cleanly to the shell prompt.

---

## 7. Dual Execution Modes

### Thread Mode (Default)
Launched with `./terminator`:
- Runs inside a single parent application process.
- The GUI runs on the main thread, handling X11 operations and the event loop.
- Each tab shell runs in a separate POSIX thread spawned via `pthread_create()`.
- Commands run as child processes via `fork() + execvp()`.
- Thread safety:
  - Working directory isolation: `chdir()` is called only inside child processes after `fork()`.
  - Shared history is protected by a POSIX mutex (`pthread_mutex_t`).

### Process Mode
Launched with `./terminator useProcess=True`:
- The GUI runs in the parent process.
- Each tab is spawned via `fork()` as a standalone child process running `shell_process_entry()`.
- GUI-to-shell communication occurs over standard IPC pipes.
- Working directory isolation: Each tab process maintains its own independent process memory and file descriptors.
- Shared history is allocated via anonymous shared memory (`mmap` with `MAP_SHARED | MAP_ANONYMOUS`) and synchronized using a process-shared POSIX semaphore (`sem_t` with `pshared = 1`).
- Fault tolerance: A crash or exit in one tab process does not crash other tabs or the parent GUI process.

---

## 8. Hotkeys and Custom Configuration (Task 7)

Terminator provides a modular keybinding architecture (`src/hotkeys.h`, `src/hotkeys.c`):
- **Default Keybindings**:
  - `Ctrl+A` / `Ctrl+E`: Jump cursor to start / end of input buffer (`cursor_pos = 0` / `cursor_pos = input_len`).
  - `↑` / `↓`: Browse shared command history through `history_get_entry()`, dynamically updating the active input line.
  - `Ctrl+U` / `Ctrl+K`: Delete from cursor to start / end of input line using `memmove()`.
  - `Ctrl+W`: Backward word deletion from cursor.
  - `Ctrl+L`: Clear scrollback screen buffer and redraw current prompt line.
  - `Ctrl+D`: Exit active tab if input buffer is empty, or delete character under cursor.
  - `Ctrl+Shift+C` / `Ctrl+Shift+V`: Copy and paste using standard X11 selection protocol (`XA_PRIMARY` and `CLIPBOARD` selection atoms).
- **Configuration File (`~/.terminatorrc`)**:
  - Parsed on application launch without requiring recompilation.
  - Format: `<action_name> = <modifier>+<key>` (e.g. `clear_screen = Ctrl+L`, `move_start = Ctrl+A`).
  - Invalid syntax or keybinding conflicts are reported to stderr and safely fall back to default bindings.

---

## 9. Signals and Job Control (Task 8)

Because Terminator runs in an X11 window without relying on a Linux PTY driver, signal generation from keystrokes is explicitly managed by the shell engine (`src/jobs.h`, `src/jobs.c`, `src/execute.c`):
- **Process Groups**:
  - Every pipeline or command is assigned its own process group using `setpgid()`.
- **Foreground Monitoring & Signal Delivery**:
  - While a foreground command runs, the shell monitors the child process status with `waitpid(..., WNOHANG | WUNTRACED)` while polling the GUI input descriptor (`in_fd`).
  - When `Ctrl+C` (`\x03`) arrives: `kill(-pgid, SIGINT)` is called, interrupting the process group.
  - When `Ctrl+Z` (`\x1a`) arrives: `kill(-pgid, SIGTSTP)` is called, suspending the process group. `waitpid` detects `WIFSTOPPED()`, adds the job to the tab's jobs table in `JOB_STOPPED` state, prints `[job_id]+ Stopped <cmd>`, and returns to the prompt.
- **Job Control Primitives**:
  - `cmd &`: Detects trailing `&`, launches the pipeline in its own process group without waiting, registers it as `JOB_RUNNING`, and immediately reprompts.
  - `jobs`: Reaps finished background jobs with `WNOHANG` and prints the list of running and stopped jobs.
  - `bg [%id]`: Sends `kill(-pgid, SIGCONT)`, updates state to `JOB_RUNNING`, and prints `[id]+ <cmd> &`.
  - `fg [%id]`: Sends `kill(-pgid, SIGCONT)` if stopped, prints the command name, and resumes foreground event monitoring.

---

## 10. Searchable History (Task 9)

Terminator provides a shared, persistent, and searchable command history architecture (`src/history.h`, `src/task_9_history.c`):
- **Storage and Capacity**:
  - Capacity: Fixed circular buffer storing up to 10,000 commands (`HISTORY_CAPACITY = 10000`).
  - Persistence: Backed on disk by `~/.terminator_history` (or custom file configured via `$HISTFILE`).
  - Startup / Cleanup: Loads existing history lines from disk on startup and cleanly flushes state back to disk on shutdown.
  - Append-on-execution: Successfully entered commands are immediately appended to disk and the in-memory buffer.
- **Search Algorithm (LCS / Substring)**:
  - Exact match lookup scanning recent commands.
  - Longest Common Subsequence (LCS) substring search to find commands sharing the longest common character pattern.
  - Interactive search mode triggered via `Ctrl+R`.
- **Command Output Limit**:
  - The `history` builtin formats and prints up to the last 1,000 commands with 1-based sequential indices.
- **Mode-Agnostic Concurrency**:
  - **Thread Mode**: Thread-safe access synchronized with a `pthread_mutex_t`.
  - **Process Mode**: Stored in a POSIX anonymous shared memory mapping (`mmap(MAP_SHARED | MAP_ANONYMOUS)`) synchronized across child processes via a process-shared POSIX semaphore (`sem_t`).


