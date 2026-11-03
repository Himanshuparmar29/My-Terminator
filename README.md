# Terminator — A Custom Terminal in C (Tasks 1–9)

Terminator is a custom graphical terminal emulator and shell engine built in C for Linux. It combines an X11 window interface with an integrated POSIX command execution pipeline, multi-tab support, I/O redirection, nested command substitution, a parallel live-output monitoring command (`multiWatch`), and dual execution architectures: **Thread Mode** and **Process Mode**.

Developed for **CS69201 Computing Lab**, Department of Computer Science and Engineering, IIT Kharagpur.

---

## 1. Project Overview

Terminator acts as both terminal and shell:
- **Terminal**: Draws its own graphical interface using core Xlib, capturing keystrokes and mouse events without depending on standard pseudo-terminals (TTY/PTY).
- **Shell**: Parses commands, expands environment variables and command substitutions, sets up unidirectional pipes, handles file redirection, and manages child processes via POSIX system calls (`fork`, `execvp`, `dup2`, `waitpid`, `pipe`, `open`, `poll`).
- **Dual Architecture**:
  - **Thread Mode (default)**: Single application process where tabs run as separate POSIX threads sharing process memory and synchronized via mutexes.
  - **Process Mode (`useProcess=True`)**: GUI runs in the parent process, while each tab runs in its own forked shell process communicating through dedicated IPC pipes, with shared history stored in POSIX shared memory synchronized by a process-shared semaphore.

---

## 2. Dependencies & Installation

### Linux / Ubuntu / WSL2 Dependencies
Building Terminator requires standard C compilation tools, POSIX thread support, and X11 development headers:

```bash
sudo apt-get update
sudo apt-get install -y build-essential libx11-dev xfonts-base
```

### X11 Display Server
Terminator is an X11 client. Ensure an X server or Wayland X11 bridge is available:
- **Native Linux (X11 / Wayland)**: `DISPLAY` is configured automatically (e.g. `:0` or `:1`).
- **WSL2 with WSLg**: Standard Windows 11 installs provide WSLg with an automatic X11 socket at `/tmp/.X11-unix/X0` and `DISPLAY=:0`.
- **Headless Servers / CI**: A virtual framebuffer (such as `Xvfb :99 & export DISPLAY=:99`) can be used to run the graphical interface.

---

## 3. Build Instructions

A `Makefile` is provided in the project root:

```bash
# Build the main terminator executable
make

# Run the automated test suite (Unit tests, Mode tests, GUI tests)
make test

# Clean all build objects, test binaries, and targets
make clean
```

The build produces the binary `./terminator`.

---

## 4. Launching Terminator

### Thread Mode (Default)
In Thread Mode, the GUI runs on the main thread and each tab shell runs in a separate POSIX thread:

```bash
./terminator
```

### Process Mode
In Process Mode, each tab runs as an isolated forked process:

```bash
./terminator useProcess=True
```

---

## 5. Implemented Features (Tasks 1–9)

### Task 1 — X11 GUI and Multiple Tabs
- Custom Xlib window rendered with dark terminal styling (1050x680 default viewport).
- Large font rendering: Automatically loads large, clear font (`-*-fixed-bold-r-*-*-28-*`, `-misc-fixed-bold-r-normal--24-*`, or `12x24`).
- Non-blocking `poll()` event loop combining the X11 server connection descriptor (`ConnectionNumber(dpy)`) with tab output pipes.
- Interactive Tab Bar at the top: `[ 1: ~/dir x ] [ 2: /tmp x ] [ + ]`.
- Mouse tab switching, tab closure via `x`, and `[ + ]` tab creation inheriting the current tab's directory.
- Keyboard shortcuts: `Alt+1`..`Alt+9` to switch tabs, `Ctrl+Shift+T` to open a tab, `Ctrl+Shift+W` to close a tab.
- Scrollback support via mouse scroll wheel (`Button4` / `Button5`) and `PageUp` / `PageDown`.
- Hidden tabs continue running background processes and accumulating output.

### Task 2 — Built-in and External Commands
- External commands executed using `fork()`, `execvp()`, and `waitpid()`.
- Captures output via pipes and renders directly in the tab text buffer.
- Dynamic Prompt: Displays `user@terminator:<dir>> `, updating live whenever the directory changes (abbreviating `$HOME` as `~`).
- Built-ins:
  - `cd [dir]`: Changes only the current tab's working directory. Supports `cd`, `cd ~`, `cd ~/path`, `cd ..`, and relative/absolute paths.
  - `exit`: Closes the active tab. Terminator exits cleanly when the last tab closes.
  - `history`: Lists shared command history numbered sequentially.
  - `multiWatch`: Parallel command watcher (see Task 6).
- Unrecognized commands print `terminator: <cmd>: command not found` with exit status `127`.
- Exit status preserved in `$?`.

### Task 3 — Multiline Input, Quoting, and Command Substitution
- Detection of incomplete commands when `Enter` is pressed:
  - Trailing unescaped backslash `\`.
  - Unclosed single quotes `'...`.
  - Unclosed double quotes `"..."`.
  - Unclosed command substitution `$(...` or `` `... ``.
- Displays continuation prompt `> ` on incomplete lines.
- Quoting semantics:
  - Single quotes `'...'`: Completely literal. Variables and substitutions are not expanded.
  - Double quotes `"..."`: Preserves whitespace, expands environment variables (`$VAR`, `${VAR}`) and `$?`.
- Nested Command Substitution:
  - Evaluates `$(...)` and `` `...` `` from the inside outward.
  - Example: `echo "Files in $(basename $(pwd)): $(ls | wc -l)"`.

### Task 4 — Input and Output Redirection
- `< infile`: Redirects standard input. Reports missing file and aborts execution if not found (`$? = 1`).
- `> outfile`: Redirects standard output, truncating or creating the file with permissions `0644`.
- Combined redirection: `./a.out < in.txt > out.txt`.
- Quoted filenames with spaces supported: `cat < "my file.txt"`.
- Executed inside child processes via `open()`, `dup2()`, and `close()`.

### Task 5 — Pipelines
- Supports arbitrary pipeline lengths: $N$ commands connected via $N-1$ pipes and $N$ children.
- Proper cleanup of unused pipe file descriptors in parent and all children to ensure EOF delivery.
- Combined with redirection on first/last commands (`cat in.txt | grep text | wc -l > count.txt`).
- GUI remains responsive while pipelines execute.

### Task 6 — multiWatch
- Syntax: `multiWatch ["cmd1", "cmd2", ...]`.
- Executes all commands in parallel with independent pipes.
- Non-blocking `poll()` on all pipes.
- Re-runs completed commands after approximately 2 seconds.
- Output layout:
  ```text
  "cmd1", <unix_timestamp>:
  ----------------------------------------------------
  <command output>
  ----------------------------------------------------
  ```
- Ctrl+C handling: Pressing `Ctrl+C` sends SIGINT/SIGKILL to process groups of all watched commands, prints `^C`, terminates multiWatch, and returns cleanly to the shell prompt without affecting other tabs.

### Task 7 — Hotkeys & Custom Configuration
- Bash-style line editing keybindings:
  - `←` / `→`: Move within the line.
  - `Ctrl+A` / `Ctrl+E`: Jump to the start / end of the input line.
  - `↑` / `↓`: Browse shared command history.
  - `Ctrl+U` / `Ctrl+K`: Delete from cursor to start / end of the line.
  - `Ctrl+W`: Delete the previous word before cursor.
  - `Ctrl+L`: Clear the screen and refresh current prompt.
  - `Ctrl+D`: Exit the shell / close the active tab when input is empty.
  - `Ctrl+Shift+C` / `Ctrl+Shift+V`: Copy and paste using X11 selections (`XA_PRIMARY` / `CLIPBOARD`).
- Config file `~/.terminatorrc`:
  - Users can customize keybindings without recompiling (e.g. `clear_screen = Ctrl+L`, `move_start = Ctrl+A`).
  - Automatically loads on startup.
  - Detects invalid key syntax or conflicting keybindings, prints a warning to stderr, and safely falls back to defaults.

### Task 8 — Signals and Job Control
- `Ctrl+C`: Terminates the running foreground process group via `kill(SIGINT)` and immediately returns to the shell prompt. Terminator itself never exits.
- `Ctrl+Z`: Suspends the running foreground command via `kill(SIGTSTP)`, prints `^Z` and `[job_id]+ Stopped <cmd>`, records it in the jobs table, and returns to the prompt.
- `cmd &`: Starts a job directly in the background, prints `[job_id] <pid>`, and immediately returns prompt.
- `jobs`: Lists all active background and stopped jobs (`[1]+ Running sleep 100 &`, `[2]+ Stopped cat`).
- `bg [%id]`: Resumes a stopped job in the background using `kill(SIGCONT)` and prints `[id]+ <cmd> &`.
- `fg [%id]`: Brings a job to the foreground, prints its command line, resumes execution if stopped, and monitors it until completion, `Ctrl+C` interrupt, or `Ctrl+Z` suspension.

### Task 9 — Searchable History
- Persistent disk storage in `~/.terminator_history` (or the file indicated by `$HISTFILE`), supporting up to 10,000 commands.
- Commands automatically loaded into memory at startup and appended after execution or synchronized on exit.
- Circular buffer capacity of 10,000 entries; older entries are pruned cleanly when capacity is reached.
- Longest Common Subsequence (LCS) / common substring search algorithm for finding historical commands matching typed patterns.
- Interactive search mode triggered with `Ctrl+R`.
- `history` builtin displays up to the last 1,000 commands sequentially numbered with 1-based indexing.
- Thread-safe and process-safe synchronization:
  - In **Thread Mode**: Mutex-locked access to the shared history buffer.
  - In **Process Mode**: POSIX shared memory (`mmap`) synchronized via a process-shared semaphore (`sem_t`).

---

## 6. Testing

An automated test suite is provided in `tests/`:

```bash
# Run all automated tests
./tests/test_runner.sh
```

The test runner automatically verifies:
1. **Compilation**: Clean build with `-Wall -Wextra` (zero compiler warnings).
2. **Unit Tests & Harness (`tests/test_harness`)**: 71 automated checks covering multiline detection, variable expansion, innermost substitutions, I/O redirection, pipelines, built-ins, `$?`, multiWatch Ctrl+C handling, hotkey bindings and matching, job control state transitions, and Task 9 searchable history (exact match, LCS substring matching, deduplication, 1000-entry print limit).
3. **Mode Tests (`tests/test_modes`)**: Verification of independent working directories, shared command history, and tab crash/destruction isolation in both Thread Mode and Process Mode.
4. **Code Quality**: Strict enforcement of zero comments in all C source and header files.
5. **GUI Launch (`tests/test_x11_gui.py`)**: Verification of live X11 window creation and event processing in both modes.

---

## 7. Known Limitations & Explicit Exclusions

As specified in project requirements (Tasks 1–9 only):
- **Autocomplete (Task 10)**: Tab file-name completion is excluded.
- **Script Interpreter (Task 11)**: Custom Bash-like interpreter for `if/elif/else` scripts is excluded.
