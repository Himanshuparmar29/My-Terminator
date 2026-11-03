#include "history.h"

static SharedHistory *g_history = NULL;
static ExecutionMode g_mode = MODE_THREAD;

static void history_lock(void) {
    if (!g_history) return;
    if (g_mode == MODE_PROCESS) sem_wait(&g_history->proc_sem);
    else pthread_mutex_lock(&g_history->thread_mutex);
}

static void history_unlock(void) {
    if (!g_history) return;
    if (g_mode == MODE_PROCESS) sem_post(&g_history->proc_sem);
    else pthread_mutex_unlock(&g_history->thread_mutex);
}

static void get_histfile(char *buf, size_t sz) {
    const char *hf = getenv("HISTFILE");
    if (hf && strlen(hf) > 0) { snprintf(buf, sz, "%s", hf); return; }
    const char *h = getenv("HOME");
    snprintf(buf, sz, "%s/.terminator_history", h ? h : ".");
}

static void history_load_file(void) {
    char path[512];
    get_histfile(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), f)) {
        size_t l = strlen(line);
        while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = '\0';
        if (l == 0) continue;
        if (g_history->count < HISTORY_CAPACITY) {
            snprintf(g_history->entries[g_history->count++], MAX_LINE_LEN, "%s", line);
        } else {
            memmove(g_history->entries[0], g_history->entries[1], sizeof(g_history->entries[0]) * (HISTORY_CAPACITY - 1));
            snprintf(g_history->entries[HISTORY_CAPACITY - 1], MAX_LINE_LEN, "%s", line);
        }
    }
    fclose(f);
}

static void history_append_file(const char *cmd) {
    char path[512];
    get_histfile(path, sizeof(path));
    FILE *f = fopen(path, "a");
    if (f) {
        fprintf(f, "%s\n", cmd);
        fclose(f);
    }
}

static void history_rewrite_file(void) {
    char path[512];
    get_histfile(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) return;
    int start = (g_history->count > HISTORY_CAPACITY) ? (g_history->count - HISTORY_CAPACITY) : 0;
    for (int i = start; i < g_history->count; i++) {
        fprintf(f, "%s\n", g_history->entries[i]);
    }
    fclose(f);
}

void history_init(ExecutionMode mode) {
    g_mode = mode;
    if (mode == MODE_PROCESS) {
        g_history = mmap(NULL, sizeof(SharedHistory),
                         PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (g_history == MAP_FAILED) {
            perror("mmap shared history");
            exit(1);
        }
        memset(g_history, 0, sizeof(SharedHistory));
        sem_init(&g_history->proc_sem, 1, 1);
    } else {
        g_history = malloc(sizeof(SharedHistory));
        if (!g_history) {
            perror("malloc history");
            exit(1);
        }
        memset(g_history, 0, sizeof(SharedHistory));
        pthread_mutex_init(&g_history->thread_mutex, NULL);
    }
    history_load_file();
}

void history_cleanup(void) {
    if (!g_history) return;
    history_rewrite_file();
    if (g_mode == MODE_PROCESS) {
        sem_destroy(&g_history->proc_sem);
        munmap(g_history, sizeof(SharedHistory));
    } else {
        pthread_mutex_destroy(&g_history->thread_mutex);
        free(g_history);
    }
    g_history = NULL;
}

void history_add(const char *cmd) {
    if (!cmd || strlen(cmd) == 0) return;
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    if (*cmd == '\0') return;

    history_lock();
    if (!g_history) return;
    if (g_history->count > 0 && strcmp(g_history->entries[g_history->count - 1], cmd) == 0) {
        history_unlock();
        return;
    }
    if (g_history->count < HISTORY_CAPACITY) {
        snprintf(g_history->entries[g_history->count++], MAX_LINE_LEN, "%s", cmd);
    } else {
        memmove(g_history->entries[0], g_history->entries[1], sizeof(g_history->entries[0]) * (HISTORY_CAPACITY - 1));
        snprintf(g_history->entries[HISTORY_CAPACITY - 1], MAX_LINE_LEN, "%s", cmd);
    }
    history_append_file(cmd);
    history_unlock();
}

void history_print(int out_fd) {
    history_lock();
    int start = (g_history->count > 1000) ? (g_history->count - 1000) : 0;
    for (int i = start; i < g_history->count; i++) {
        dprintf(out_fd, "%5d  %s\n", i + 1, g_history->entries[i]);
    }
    history_unlock();
}

int history_get_count(void) {
    history_lock();
    int c = g_history ? g_history->count : 0;
    history_unlock();
    return c;
}

int history_get_entry(int index, char *buf, size_t max_len) {
    int res = 0;
    history_lock();
    if (g_history && index >= 0 && index < g_history->count) {
        snprintf(buf, max_len, "%s", g_history->entries[index]);
        res = 1;
    }
    history_unlock();
    return res;
}

static int lcs_length(const char *s1, const char *s2) {
    int l1 = strlen(s1), l2 = strlen(s2), max = 0;
    for (int i = 0; i < l1; i++) {
        for (int j = 0; j < l2; j++) {
            int k = 0;
            while (i + k < l1 && j + k < l2 && s1[i + k] == s2[j + k]) k++;
            if (k > max) max = k;
        }
    }
    return max;
}

int history_search(const char *term, char results[][MAX_LINE_LEN], int max_results) {
    if (!term || strlen(term) == 0 || max_results <= 0) return 0;
    history_lock();
    if (!g_history || g_history->count == 0) {
        history_unlock();
        return 0;
    }
    for (int i = g_history->count - 1; i >= 0; i--) {
        if (strcmp(g_history->entries[i], term) == 0) {
            snprintf(results[0], MAX_LINE_LEN, "%s", g_history->entries[i]);
            history_unlock();
            return 1;
        }
    }
    int best_len = 0;
    for (int i = 0; i < g_history->count; i++) {
        int l = lcs_length(term, g_history->entries[i]);
        if (l > best_len) best_len = l;
    }
    if (best_len <= 2) {
        history_unlock();
        return 0;
    }
    int count = 0;
    for (int i = 0; i < g_history->count && count < max_results; i++) {
        if (lcs_length(term, g_history->entries[i]) == best_len) {
            int dup = 0;
            for (int d = 0; d < count; d++) {
                if (strcmp(results[d], g_history->entries[i]) == 0) { dup = 1; break; }
            }
            if (!dup) {
                snprintf(results[count++], MAX_LINE_LEN, "%s", g_history->entries[i]);
            }
        }
    }
    history_unlock();
    return count;
}
