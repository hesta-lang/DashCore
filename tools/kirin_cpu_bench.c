#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    int cpu_id;
    uint64_t iterations;
    uint64_t elapsed_ns;
    uint64_t checksum;
    int affinity_error;
} worker_result;

static atomic_int start_workers = 0;
static uint64_t duration_ns = 0;

static uint64_t monotonic_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static void *run_worker(void *opaque) {
    worker_result *result = (worker_result *)opaque;
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET(result->cpu_id, &cpus);
    result->affinity_error = pthread_setaffinity_np(
        pthread_self(), sizeof(cpus), &cpus
    );

    while (atomic_load_explicit(&start_workers, memory_order_acquire) == 0) {
        sched_yield();
    }

    const uint64_t start = monotonic_ns();
    uint64_t now = start;
    uint64_t state = UINT64_C(0x9E3779B97F4A7C15) ^
                     ((uint64_t)(unsigned int)result->cpu_id + 1U);
    uint64_t iterations = 0;

    while (now >= start && (now - start) < duration_ns) {
        for (unsigned int i = 0; i < 4096U; ++i) {
            state ^= state >> 12;
            state ^= state << 25;
            state ^= state >> 27;
            state *= UINT64_C(0x2545F4914F6CDD1D);
            state += UINT64_C(0xD1B54A32D192ED03);
        }
        iterations += 4096U;
        now = monotonic_ns();
    }

    const uint64_t finish = monotonic_ns();
    result->iterations = iterations;
    result->elapsed_ns = (finish >= start) ? finish - start : 0;
    result->checksum = state;
    return NULL;
}

static int parse_cpu_list(const char *text, int *cpus, size_t capacity,
                          size_t *count) {
    if (text == NULL || cpus == NULL || count == NULL || capacity == 0) {
        return -1;
    }

    char *copy = strdup(text);
    if (copy == NULL) {
        return -1;
    }

    size_t used = 0;
    char *save = NULL;
    for (char *token = strtok_r(copy, ",", &save);
         token != NULL;
         token = strtok_r(NULL, ",", &save)) {
        char *end = NULL;
        errno = 0;
        long value = strtol(token, &end, 10);
        if (errno != 0 || end == token || *end != '\0' ||
            value < 0 || value >= CPU_SETSIZE || used >= capacity) {
            free(copy);
            return -1;
        }
        for (size_t i = 0; i < used; ++i) {
            if (cpus[i] == (int)value) {
                free(copy);
                return -1;
            }
        }
        cpus[used++] = (int)value;
    }

    free(copy);
    if (used == 0) {
        return -1;
    }
    *count = used;
    return 0;
}

int main(int argc, char **argv) {
    const char *cpu_text = NULL;
    unsigned long duration_ms = 2500UL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--cpus") == 0 && i + 1 < argc) {
            cpu_text = argv[++i];
        } else if (strcmp(argv[i], "--duration-ms") == 0 && i + 1 < argc) {
            char *end = NULL;
            errno = 0;
            duration_ms = strtoul(argv[++i], &end, 10);
            if (errno != 0 || end == argv[i] || *end != '\0' ||
                duration_ms < 500UL || duration_ms > 60000UL) {
                fprintf(stderr, "Invalid --duration-ms (expected 500..60000).\n");
                return 2;
            }
        } else {
            fprintf(stderr,
                    "Usage: %s --cpus 0,1,... [--duration-ms 2500]\n",
                    argv[0]);
            return 2;
        }
    }

    int cpus[CPU_SETSIZE];
    size_t count = 0;
    if (parse_cpu_list(cpu_text, cpus, CPU_SETSIZE, &count) != 0) {
        fprintf(stderr, "Invalid or empty CPU list.\n");
        return 2;
    }

    pthread_t *threads = calloc(count, sizeof(*threads));
    worker_result *results = calloc(count, sizeof(*results));
    if (threads == NULL || results == NULL) {
        fprintf(stderr, "Unable to allocate worker state.\n");
        free(threads);
        free(results);
        return 2;
    }

    duration_ns = (uint64_t)duration_ms * UINT64_C(1000000);
    size_t created = 0;
    for (size_t i = 0; i < count; ++i) {
        results[i].cpu_id = cpus[i];
        const int rc = pthread_create(&threads[i], NULL, run_worker, &results[i]);
        if (rc != 0) {
            fprintf(stderr, "pthread_create failed for CPU %d: %s\n",
                    cpus[i], strerror(rc));
            break;
        }
        ++created;
    }

    if (created != count) {
        atomic_store_explicit(&start_workers, 1, memory_order_release);
        for (size_t i = 0; i < created; ++i) {
            (void)pthread_join(threads[i], NULL);
        }
        free(threads);
        free(results);
        return 2;
    }

    atomic_store_explicit(&start_workers, 1, memory_order_release);

    int affinity_failed = 0;
    for (size_t i = 0; i < count; ++i) {
        (void)pthread_join(threads[i], NULL);
        if (results[i].affinity_error != 0) {
            affinity_failed = 1;
        }
        const double elapsed_ms = (double)results[i].elapsed_ns / 1000000.0;
        const double rate = results[i].elapsed_ns > 0
            ? ((double)results[i].iterations * 1000000000.0) /
              (double)results[i].elapsed_ns
            : 0.0;
        printf("{\"cpu\":%d,\"iterations\":%" PRIu64
               ",\"elapsed_ms\":%.3f,\"iterations_per_second\":%.3f"
               ",\"affinity_ok\":%s,\"affinity_error\":%d"
               ",\"checksum\":\"%" PRIx64 "\"}\n",
               results[i].cpu_id, results[i].iterations, elapsed_ms, rate,
               results[i].affinity_error == 0 ? "true" : "false",
               results[i].affinity_error, results[i].checksum);
    }

    free(threads);
    free(results);
    return affinity_failed ? 3 : 0;
}
