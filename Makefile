CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99 -D_GNU_SOURCE -Isrc
LDFLAGS = -lX11 -pthread

SRC = src/main.c \
      src/task_1_gui.c \
      src/task_1_tab.c \
      src/task_2_shell.c \
      src/task_3_parse.c \
      src/task_4_5_execute.c \
      src/task_6_multiwatch.c \
      src/task_7_hotkeys.c \
      src/task_8_jobs.c \
      src/task_9_history.c

OBJ = $(SRC:.c=.o)
TARGET = terminator

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: tests/test_suite tests/test_modes tests/test_harness
	./tests/test_suite
	./tests/test_modes
	./tests/test_harness

tests/test_suite: tests/test_suite.c src/task_3_parse.c src/task_4_5_execute.c src/task_6_multiwatch.c src/task_9_history.c src/task_8_jobs.c src/task_7_hotkeys.c
	$(CC) $(CFLAGS) -Itests $^ -o $@ $(LDFLAGS)

tests/test_modes: tests/test_modes.c src/task_1_tab.c src/task_2_shell.c src/task_3_parse.c src/task_4_5_execute.c src/task_6_multiwatch.c src/task_9_history.c src/task_8_jobs.c src/task_7_hotkeys.c
	$(CC) $(CFLAGS) -Itests $^ -o $@ $(LDFLAGS)

tests/test_harness: tests/test_harness.c src/task_1_tab.c src/task_2_shell.c src/task_3_parse.c src/task_4_5_execute.c src/task_6_multiwatch.c src/task_9_history.c src/task_8_jobs.c src/task_7_hotkeys.c
	$(CC) $(CFLAGS) -Itests $^ -o $@ $(LDFLAGS)

clean:
	rm -f $(OBJ) src/*.o $(TARGET) tests/test_suite tests/test_modes tests/test_harness tests/*.o

.PHONY: all clean test
