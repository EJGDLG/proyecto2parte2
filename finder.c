// finder.c
// Compilar:  gcc -O2 -pthread finder.c -o finder
// Ejecutar:  ./finder base length variant workers
// variant: dynamic | round_robin | random_static

#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#define IS_WINDOWS 1
#else
#define IS_WINDOWS 0
#endif

// ======== Config ========
static const char *ALPHABET = "abcdefghijklmnopqrstuvwxyz0123456789";

typedef struct {
    char **items;
    int capacity;
    int head;
    int tail;
    int count;
    int closed;
    pthread_mutex_t mtx;
    pthread_cond_t cv_nonempty;
    pthread_cond_t cv_nonfull;
} queue_t;

typedef struct {
    char *base;
    queue_t *q;
    char **found;
    int *found_count;
    pthread_mutex_t *found_mtx;
} dyn_worker_args;

typedef struct {
    char *base;
    char **chunk;
    int n;
    char **found;
    int *found_count;
    pthread_mutex_t *found_mtx;
} static_worker_args;

// ----- util -----
static int ping_host(const char *host) {
    // Ejecuta 'ping' 1 vez con timeout corto
    char cmd[512];
    if (IS_WINDOWS) {
        snprintf(cmd, sizeof(cmd), "ping -n 1 -w 800 %s > NUL", host);
    } else {
        snprintf(cmd, sizeof(cmd), "ping -c 1 -W 1 %s > /dev/null 2>&1", host);
    }
    int code = system(cmd);
    return code == 0;
}

static void shuffle(char **arr, int n) {
    for (int i = n - 1; i > 0; --i) {
        int j = rand() % (i + 1);
        char *tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp;
    }
}

// ----- queue -----
static void q_init(queue_t *q, int capacity) {
    q->items = (char**)malloc(sizeof(char*) * capacity);
    q->capacity = capacity;
    q->head = q->tail = q->count = 0;
    q->closed = 0;
    pthread_mutex_init(&q->mtx, NULL);
    pthread_cond_init(&q->cv_nonempty, NULL);
    pthread_cond_init(&q->cv_nonfull, NULL);
}

static void q_close(queue_t *q) {
    pthread_mutex_lock(&q->mtx);
    q->closed = 1;
    pthread_cond_broadcast(&q->cv_nonempty);
    pthread_mutex_unlock(&q->mtx);
}

static void q_destroy(queue_t *q) {
    free(q->items);
    pthread_mutex_destroy(&q->mtx);
    pthread_cond_destroy(&q->cv_nonempty);
    pthread_cond_destroy(&q->cv_nonfull);
}

static int q_push(queue_t *q, char *item) {
    pthread_mutex_lock(&q->mtx);
    while (q->count == q->capacity && !q->closed) {
        pthread_cond_wait(&q->cv_nonfull, &q->mtx);
    }
    if (q->closed) { pthread_mutex_unlock(&q->mtx); return 0; }
    q->items[q->tail] = item;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    pthread_cond_signal(&q->cv_nonempty);
    pthread_mutex_unlock(&q->mtx);
    return 1;
}

static char* q_pop(queue_t *q) {
    pthread_mutex_lock(&q->mtx);
    while (q->count == 0 && !q->closed) {
        pthread_cond_wait(&q->cv_nonempty, &q->mtx);
    }
    if (q->count == 0 && q->closed) { pthread_mutex_unlock(&q->mtx); return NULL; }
    char *it = q->items[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    pthread_cond_signal(&q->cv_nonfull);
    pthread_mutex_unlock(&q->mtx);
    return it;
}

// ----- workers -----
static void *dyn_worker(void *arg) {
    dyn_worker_args *a = (dyn_worker_args*)arg;
    for (;;) {
        char *suf = q_pop(a->q);
        if (!suf) break; // cola cerrada y vacía
        // construir host
        size_t len = strlen(a->base) + strlen(suf) + 1;
        char *host = (char*)malloc(len);
        snprintf(host, len, "%s%s", a->base, suf);
        if (ping_host(host)) {
            pthread_mutex_lock(a->found_mtx);
            a->found[*a->found_count] = strdup(suf);
            (*a->found_count)++;
            pthread_mutex_unlock(a->found_mtx);
        }
        free(host);
    }
    return NULL;
}

static void *static_worker(void *arg) {
    static_worker_args *a = (static_worker_args*)arg;
    for (int i = 0; i < a->n; ++i) {
        const char *suf = a->chunk[i];
        size_t len = strlen(a->base) + strlen(suf) + 1;
        char *host = (char*)malloc(len);
        snprintf(host, len, "%s%s", a->base, suf);
        if (ping_host(host)) {
            pthread_mutex_lock(a->found_mtx);
            a->found[*a->found_count] = strdup(suf);
            (*a->found_count)++;
            pthread_mutex_unlock(a->found_mtx);
        }
        free(host);
    }
    return NULL;
}

// ----- generación de sufijos -----
static char **gen_suffixes(int length, long long *out_n) {
    long long total = 1;
    int alpha = 36; // a-z + 0-9
    for (int i = 0; i < length; ++i) total *= alpha;

    char **arr = (char**)malloc(sizeof(char*) * total);
    long long idx = 0;

    // iteración tipo odómetro base-36
    int *digits = (int*)calloc(length, sizeof(int));
    for (;;) {
        char *s = (char*)malloc(length + 1);
        for (int i = 0; i < length; ++i) {
            s[i] = ALPHABET[digits[i]];
        }
        s[length] = '\0';
        arr[idx++] = s;

        // incrementar
        int pos = length - 1;
        while (pos >= 0) {
            digits[pos]++;
            if (digits[pos] < alpha) break;
            digits[pos] = 0;
            pos--;
        }
        if (pos < 0) break;
    }
    free(digits);
    *out_n = idx;
    return arr;
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "Uso: %s <base> <length> <variant> <workers>\n", argv[0]);
        fprintf(stderr, "variant: dynamic | round_robin | random_static\n");
        return 1;
    }
    char *base = argv[1];
    int length = atoi(argv[2]);
    char *variant = argv[3];
    int workers = atoi(argv[4]);
    if (workers < 1) workers = 1;

    srand((unsigned)time(NULL));

    long long n_suf = 0;
    char **suf = gen_suffixes(length, &n_suf);

    // arreglo para resultados (máximo n_suf, en práctica serán pocos)
    char **found = (char**)malloc(sizeof(char*) * (size_t)n_suf);
    int found_count = 0;
    pthread_mutex_t found_mtx;
    pthread_mutex_init(&found_mtx, NULL);

    if (strcmp(variant, "dynamic") == 0) {
        // Variante dinámica: cola + barajado para evitar orden fijo
        shuffle(suf, (int)n_suf);

        queue_t q;
        q_init(&q, 4 * workers);

        pthread_t *ts = (pthread_t*)malloc(sizeof(pthread_t) * workers);
        dyn_worker_args a = { base, &q, found, &found_count, &found_mtx };
        for (int i = 0; i < workers; ++i) {
            pthread_create(&ts[i], NULL, dyn_worker, &a);
        }
        for (long long i = 0; i < n_suf; ++i) q_push(&q, suf[i]);
        q_close(&q);
        for (int i = 0; i < workers; ++i) pthread_join(ts[i], NULL);

        q_destroy(&q);
        free(ts);

    } else {
        // Variante estática: bloques (secuencial u orden barajado)
        if (strcmp(variant, "random_static") == 0) {
            shuffle(suf, (int)n_suf);
        }
        long long chunk = (n_suf + workers - 1) / workers;
        pthread_t *ts = (pthread_t*)malloc(sizeof(pthread_t) * workers);
        static_worker_args *args = (static_worker_args*)malloc(sizeof(static_worker_args) * workers);

        int created = 0;
        for (int i = 0; i < workers; ++i) {
            long long start = i * chunk;
            long long end = (start + chunk > n_suf) ? n_suf : start + chunk;
            if (start >= end) break;
            args[i].base = base;
            args[i].chunk = &suf[start];
            args[i].n = (int)(end - start);
            args[i].found = found;
            args[i].found_count = &found_count;
            args[i].found_mtx = &found_mtx;
            pthread_create(&ts[i], NULL, static_worker, &args[i]);
            created++;
        }
        for (int i = 0; i < created; ++i) pthread_join(ts[i], NULL);
        free(ts);
        free(args);
    }

    if (found_count == 0) {
        printf("No se encontraron hosts válidos.\n");
    } else {
        printf("Encontrados (sufijos con ping OK): ");
        for (int i = 0; i < found_count; ++i) {
            printf("%s%s", found[i], (i + 1 == found_count) ? "\n" : ", ");
            free(found[i]);
        }
    }

    // liberar sufijos
    for (long long i = 0; i < n_suf; ++i) free(suf[i]);
    free(suf);
    free(found);
    pthread_mutex_destroy(&found_mtx);
    return 0;
}
