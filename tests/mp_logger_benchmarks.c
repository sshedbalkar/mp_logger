#define _POSIX_C_SOURCE 200809L

#include "mp_logger.h"
#include "mp_logger_internal.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    const char *name;
    size_t buffer_capacity;
    size_t message_capacity;
    size_t context_capacity;
} benchmark_profile_t;

typedef struct {
    atomic_uint_fast64_t write_count;
    uint32_t delay_micros;
} benchmark_stream_t;

typedef struct {
    FILE *file_handle;
} benchmark_file_stream_t;

typedef struct {
    uint64_t attempted;
    uint64_t accepted;
    uint64_t busy;
    uint64_t full;
    uint64_t other;
    uint64_t processed;
    double elapsed_seconds;
} benchmark_run_result_t;

typedef struct {
    mp_logger_t *logger;
    atomic_uint_fast64_t attempted;
    atomic_uint_fast64_t accepted;
    atomic_uint_fast64_t busy;
    atomic_uint_fast64_t full;
    atomic_uint_fast64_t other;
    uint64_t stop_time_nanos;
} producer_context_t;

static const benchmark_profile_t benchmark_profiles[] = {
    {"small", 64u, 64u, 128u},
    {"default", 1024u, 512u, 1024u},
    {"deep", 4096u, 512u, 1024u},
};

static const char benchmark_message_text[] =
    "order pipeline committed batch=184 tenant=alpha checkout_ms=17 retry_count=2 status=ok";
static const char benchmark_context_text[] =
    "request_id=req-42 shard=2 trace=9f1d8f mode=benchmark";

static uint64_t benchmark_now_nanos(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0u;
    }
    return ((uint64_t)now.tv_sec * 1000000000ull) + (uint64_t)now.tv_nsec;
}

static void benchmark_sleep_until_nanos(uint64_t target_nanos) {
    struct timespec target;
    target.tv_sec = (time_t)(target_nanos / 1000000000ull);
    target.tv_nsec = (long)(target_nanos % 1000000000ull);
    (void)clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &target, NULL);
}

static void benchmark_ensure_directory(const char *path) {
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "failed to create directory %s: %s\n", path, strerror(errno));
        exit(1);
    }
}

static size_t benchmark_queue_reserved_bytes(const benchmark_profile_t *profile) {
    mp_logger_config_t config;
    mp_logger_config_init_defaults(&config);
    return profile->buffer_capacity *
        (sizeof(mp_log_slot_t) +
            profile->message_capacity +
            profile->context_capacity +
            (config.field_capacity * sizeof(mp_log_field_t)) +
            (config.field_capacity * config.field_key_capacity) +
            (config.field_capacity * config.field_value_capacity));
}

static size_t benchmark_worker_reserved_bytes(const benchmark_profile_t *profile) {
    mp_logger_config_t config;
    mp_logger_config_init_defaults(&config);
    return profile->message_capacity +
        profile->context_capacity +
        (config.field_capacity * sizeof(mp_log_field_t)) +
        (config.field_capacity * config.field_key_capacity) +
        (config.field_capacity * config.field_value_capacity) +
        (profile->message_capacity +
            profile->context_capacity +
            (config.field_capacity *
                (config.field_key_capacity + config.field_value_capacity + MP_LOGGER_NUMBER_TEXT_CAPACITY)) +
            MP_LOGGER_RENDER_PADDING);
}

static mp_log_status_t benchmark_stream_write(
    void *stream_context,
    const mp_log_record_t *record,
    const char *formatted_entry,
    size_t formatted_entry_length) {
    benchmark_stream_t *stream = (benchmark_stream_t *)stream_context;
    (void)record;
    (void)formatted_entry;
    (void)formatted_entry_length;
    if (stream == NULL) {
        return MP_LOG_STATUS_INVALID_ARGUMENT;
    }
    if (stream->delay_micros > 0u) {
        struct timespec wait_time;
        wait_time.tv_sec = (time_t)(stream->delay_micros / 1000000u);
        wait_time.tv_nsec = (long)((stream->delay_micros % 1000000u) * 1000u);
        (void)nanosleep(&wait_time, NULL);
    }
    atomic_fetch_add(&stream->write_count, 1u);
    return MP_LOG_STATUS_OK;
}

static void benchmark_stream_destroy(void *stream_context) {
    free(stream_context);
}

static mp_log_status_t benchmark_file_stream_write(
    void *stream_context,
    const mp_log_record_t *record,
    const char *formatted_entry,
    size_t formatted_entry_length) {
    benchmark_file_stream_t *stream = (benchmark_file_stream_t *)stream_context;
    size_t written = 0u;
    (void)record;
    if (stream == NULL || stream->file_handle == NULL || formatted_entry == NULL) {
        return MP_LOG_STATUS_INVALID_ARGUMENT;
    }
    written = fwrite(formatted_entry, 1u, formatted_entry_length, stream->file_handle);
    written += fwrite("\n", 1u, 1u, stream->file_handle);
    if (written != formatted_entry_length + 1u) {
        return MP_LOG_STATUS_IO_ERROR;
    }
    if (fflush(stream->file_handle) != 0) {
        return MP_LOG_STATUS_IO_ERROR;
    }
    return MP_LOG_STATUS_OK;
}

static void benchmark_file_stream_destroy(void *stream_context) {
    benchmark_file_stream_t *stream = (benchmark_file_stream_t *)stream_context;
    if (stream == NULL) {
        return;
    }
    if (stream->file_handle != NULL) {
        (void)fclose(stream->file_handle);
    }
    free(stream);
}

/* Build a logger with one synthetic stream so throughput cases can vary buffer depth and sink delay. */
static mp_logger_t *benchmark_create_logger(
    const char *backup_prefix,
    size_t buffer_capacity,
    size_t message_capacity,
    size_t context_capacity,
    uint32_t sink_delay_micros) {
    mp_logger_config_t config;
    mp_logger_stream_t stream;
    benchmark_stream_t *stream_state = NULL;
    mp_logger_t *logger = NULL;

    mp_logger_config_init_defaults(&config);
    config.buffer_capacity = buffer_capacity;
    config.message_capacity = message_capacity;
    config.context_capacity = context_capacity;
    (void)snprintf(config.active_streams, sizeof(config.active_streams), "%s", "");
    (void)snprintf(config.log_directory, sizeof(config.log_directory), "%s", ".tmp/benchmarks");
    (void)snprintf(config.file_name_prefix, sizeof(config.file_name_prefix), "%s", "bench");
    (void)snprintf(
        config.backup_file_name_prefix,
        sizeof(config.backup_file_name_prefix),
        "%s",
        backup_prefix);

    if (mp_logger_create(&config, &logger) != MP_LOG_STATUS_OK) {
        fprintf(stderr, "failed to create benchmark logger\n");
        exit(1);
    }

    stream_state = (benchmark_stream_t *)calloc(1u, sizeof(*stream_state));
    if (stream_state == NULL) {
        fprintf(stderr, "failed to allocate benchmark stream\n");
        mp_logger_destroy(logger);
        exit(1);
    }
    stream_state->delay_micros = sink_delay_micros;
    atomic_init(&stream_state->write_count, 0u);

    memset(&stream, 0, sizeof(stream));
    (void)snprintf(stream.stream_name, sizeof(stream.stream_name), "%s", "bench");
    stream.minimum_level = MP_LOG_LEVEL_TRACE;
    stream.maximum_level = MP_LOG_LEVEL_FATAL;
    stream.stream_context = stream_state;
    stream.write = benchmark_stream_write;
    stream.destroy = benchmark_stream_destroy;

    if (mp_logger_add_stream(logger, &stream) != MP_LOG_STATUS_OK) {
        fprintf(stderr, "failed to add benchmark stream\n");
        benchmark_stream_destroy(stream_state);
        mp_logger_destroy(logger);
        exit(1);
    }
    if (mp_logger_start(logger) != MP_LOG_STATUS_OK) {
        fprintf(stderr, "failed to start benchmark logger\n");
        mp_logger_destroy(logger);
        exit(1);
    }
    return logger;
}

/* Add extra file sinks so one benchmark run can measure sequential worker fanout cost. */
static void benchmark_add_custom_file_stream(
    mp_logger_t *logger,
    const char *stream_name,
    const char *path) {
    mp_logger_stream_t stream;
    benchmark_file_stream_t *stream_state = NULL;

    if (logger == NULL || stream_name == NULL || path == NULL) {
        fprintf(stderr, "invalid file stream parameters\n");
        exit(1);
    }

    stream_state = (benchmark_file_stream_t *)calloc(1u, sizeof(*stream_state));
    if (stream_state == NULL) {
        fprintf(stderr, "failed to allocate file stream state\n");
        exit(1);
    }
    stream_state->file_handle = fopen(path, "w");
    if (stream_state->file_handle == NULL) {
        fprintf(stderr, "failed to open benchmark file stream %s\n", path);
        benchmark_file_stream_destroy(stream_state);
        exit(1);
    }

    memset(&stream, 0, sizeof(stream));
    (void)snprintf(stream.stream_name, sizeof(stream.stream_name), "%s", stream_name);
    stream.minimum_level = MP_LOG_LEVEL_TRACE;
    stream.maximum_level = MP_LOG_LEVEL_FATAL;
    stream.stream_context = stream_state;
    stream.write = benchmark_file_stream_write;
    stream.destroy = benchmark_file_stream_destroy;

    if (mp_logger_add_stream(logger, &stream) != MP_LOG_STATUS_OK) {
        fprintf(stderr, "failed to add custom benchmark file stream\n");
        benchmark_file_stream_destroy(stream_state);
        exit(1);
    }
}

/* Start from the builtin file sink and layer on additional file streams that share the same run suffix. */
static mp_logger_t *benchmark_create_file_logger(const char *backup_prefix, size_t total_file_streams) {
    mp_logger_config_t config;
    mp_logger_t *logger = NULL;
    size_t index = 0u;

    if (total_file_streams == 0u || total_file_streams > MP_LOGGER_MAX_STREAMS) {
        fprintf(stderr, "invalid file stream count %zu\n", total_file_streams);
        exit(1);
    }

    mp_logger_config_init_defaults(&config);
    config.buffer_capacity = 8192u;
    config.message_capacity = 256u;
    config.context_capacity = 128u;
    (void)snprintf(config.active_streams, sizeof(config.active_streams), "%s", MP_LOGGER_STREAM_FILE);
    (void)snprintf(config.log_directory, sizeof(config.log_directory), "%s", ".tmp/benchmarks/file-streams");
    (void)snprintf(config.file_name_prefix, sizeof(config.file_name_prefix), "%s", "bench-file-primary");
    (void)snprintf(
        config.backup_file_name_prefix,
        sizeof(config.backup_file_name_prefix),
        "%s",
        backup_prefix);

    if (mp_logger_create(&config, &logger) != MP_LOG_STATUS_OK) {
        fprintf(stderr, "failed to create file benchmark logger\n");
        exit(1);
    }

    for (index = 1u; index < total_file_streams; index++) {
        char stream_name[MP_LOGGER_NAME_CAPACITY];
        char path[MP_LOGGER_PATH_CAPACITY];
        (void)snprintf(stream_name, sizeof(stream_name), "file-%zu", index + 1u);
        (void)snprintf(
            path,
            sizeof(path),
            ".tmp/benchmarks/file-streams/bench-file-extra-%zu-%s.log",
            index + 1u,
            logger->run_suffix);
        benchmark_add_custom_file_stream(logger, stream_name, path);
    }

    if (mp_logger_start(logger) != MP_LOG_STATUS_OK) {
        fprintf(stderr, "failed to start file benchmark logger\n");
        mp_logger_destroy(logger);
        exit(1);
    }
    return logger;
}

/* Treat flush and shutdown failures as benchmark-fatal so printed tables never mix partial results. */
static void benchmark_destroy_logger(mp_logger_t *logger) {
    if (logger == NULL) {
        return;
    }
    if (mp_logger_flush(logger, 30000u) != MP_LOG_STATUS_OK) {
        fprintf(stderr, "benchmark flush failed\n");
        mp_logger_destroy(logger);
        exit(1);
    }
    if (mp_logger_shutdown(logger, 30000u) != MP_LOG_STATUS_OK) {
        fprintf(stderr, "benchmark shutdown failed\n");
        mp_logger_destroy(logger);
        exit(1);
    }
    mp_logger_destroy(logger);
}

/* Drain the queue at measurement boundaries so processed counts line up with the benchmark window. */
static void benchmark_flush_logger_or_die(mp_logger_t *logger) {
    if (logger == NULL) {
        return;
    }
    if (mp_logger_flush(logger, 30000u) != MP_LOG_STATUS_OK) {
        fprintf(stderr, "benchmark flush failed\n");
        mp_logger_destroy(logger);
        exit(1);
    }
}

/* Bucket enqueue results so later tables can distinguish lock contention from true queue saturation. */
static void benchmark_record_status(producer_context_t *context, mp_log_status_t status) {
    atomic_fetch_add(&context->attempted, 1u);
    switch (status) {
    case MP_LOG_STATUS_OK:
        atomic_fetch_add(&context->accepted, 1u);
        break;
    case MP_LOG_STATUS_BUSY:
        atomic_fetch_add(&context->busy, 1u);
        break;
    case MP_LOG_STATUS_QUEUE_FULL:
        atomic_fetch_add(&context->full, 1u);
        break;
    default:
        atomic_fetch_add(&context->other, 1u);
        break;
    }
}

/* Run busy-loop producers until the shared stop time to stress the non-blocking admission path. */
static void *benchmark_producer_main(void *opaque_context) {
    producer_context_t *context = (producer_context_t *)opaque_context;
    while (benchmark_now_nanos() < context->stop_time_nanos) {
        mp_log_status_t status = mp_logger_log(
            context->logger,
            MP_LOG_LEVEL_INFO,
            "benchmark-message",
            "mode=concurrency");
        benchmark_record_status(context, status);
    }
    return NULL;
}

/* Measure aggregate admission and drain behavior while multiple producer threads race on one logger. */
static benchmark_run_result_t benchmark_run_concurrency_case(size_t producer_threads, uint32_t duration_millis) {
    pthread_t *threads = NULL;
    producer_context_t *contexts = NULL;
    mp_logger_t *logger = NULL;
    mp_logger_stats_t stats;
    benchmark_run_result_t result;
    uint64_t start_nanos = 0u;
    uint64_t stop_nanos = 0u;
    size_t index = 0;

    memset(&result, 0, sizeof(result));
    logger = benchmark_create_logger("bench-concurrency", 1024u, 128u, 128u, 0u);
    threads = (pthread_t *)calloc(producer_threads, sizeof(*threads));
    contexts = (producer_context_t *)calloc(producer_threads, sizeof(*contexts));
    if (threads == NULL || contexts == NULL) {
        fprintf(stderr, "failed to allocate producer threads\n");
        free(threads);
        free(contexts);
        mp_logger_destroy(logger);
        exit(1);
    }

    start_nanos = benchmark_now_nanos();
    stop_nanos = start_nanos + ((uint64_t)duration_millis * 1000000ull);

    for (index = 0; index < producer_threads; index++) {
        contexts[index].logger = logger;
        contexts[index].stop_time_nanos = stop_nanos;
        atomic_init(&contexts[index].attempted, 0u);
        atomic_init(&contexts[index].accepted, 0u);
        atomic_init(&contexts[index].busy, 0u);
        atomic_init(&contexts[index].full, 0u);
        atomic_init(&contexts[index].other, 0u);
        if (pthread_create(&threads[index], NULL, benchmark_producer_main, &contexts[index]) != 0) {
            fprintf(stderr, "failed to create producer thread\n");
            free(threads);
            free(contexts);
            mp_logger_destroy(logger);
            exit(1);
        }
    }

    for (index = 0; index < producer_threads; index++) {
        (void)pthread_join(threads[index], NULL);
        result.attempted += atomic_load(&contexts[index].attempted);
        result.accepted += atomic_load(&contexts[index].accepted);
        result.busy += atomic_load(&contexts[index].busy);
        result.full += atomic_load(&contexts[index].full);
        result.other += atomic_load(&contexts[index].other);
    }

    benchmark_flush_logger_or_die(logger);
    if (mp_logger_get_stats(logger, &stats) == MP_LOG_STATUS_OK) {
        result.processed = stats.processed_records;
    }
    benchmark_destroy_logger(logger);
    result.elapsed_seconds = (double)duration_millis / 1000.0;
    free(threads);
    free(contexts);
    return result;
}

/* Measure steady-state file throughput by driving one thread as fast as possible for a fixed duration. */
static benchmark_run_result_t benchmark_run_single_thread_file_case(
    size_t total_file_streams,
    uint32_t duration_millis) {
    mp_logger_t *logger = NULL;
    mp_logger_stats_t stats;
    benchmark_run_result_t result;
    uint64_t start_nanos = 0u;
    uint64_t stop_nanos = 0u;

    memset(&result, 0, sizeof(result));
    logger = benchmark_create_file_logger("bench-file-speed", total_file_streams);
    start_nanos = benchmark_now_nanos();
    stop_nanos = start_nanos + ((uint64_t)duration_millis * 1000000ull);

    while (benchmark_now_nanos() < stop_nanos) {
        mp_log_status_t status = mp_logger_log(
            logger,
            MP_LOG_LEVEL_INFO,
            benchmark_message_text,
            benchmark_context_text);
        result.attempted++;
        if (status == MP_LOG_STATUS_OK) {
            result.accepted++;
        } else if (status == MP_LOG_STATUS_BUSY) {
            result.busy++;
        } else if (status == MP_LOG_STATUS_QUEUE_FULL) {
            result.full++;
        } else {
            result.other++;
        }
    }

    benchmark_flush_logger_or_die(logger);
    if (mp_logger_get_stats(logger, &stats) == MP_LOG_STATUS_OK) {
        result.processed = stats.processed_records;
    }
    benchmark_destroy_logger(logger);
    result.elapsed_seconds = (double)duration_millis / 1000.0;
    return result;
}

/* Pace submissions to a target rate so the benchmark can detect the first rate that overflows. */
static benchmark_run_result_t benchmark_run_paced_rate_case(
    size_t buffer_capacity,
    uint32_t sink_delay_micros,
    uint32_t rate_per_second,
    uint32_t duration_millis) {
    mp_logger_t *logger = NULL;
    mp_logger_stats_t stats;
    benchmark_run_result_t result;
    uint64_t start_nanos = benchmark_now_nanos();
    uint64_t stop_nanos = start_nanos + ((uint64_t)duration_millis * 1000000ull);
    uint64_t interval_nanos = 1000000000ull / (uint64_t)rate_per_second;
    uint64_t scheduled_nanos = start_nanos;

    memset(&result, 0, sizeof(result));
    logger = benchmark_create_logger("bench-threshold", buffer_capacity, 128u, 128u, sink_delay_micros);

    while (scheduled_nanos < stop_nanos) {
        mp_log_status_t status;
        benchmark_sleep_until_nanos(scheduled_nanos);
        status = mp_logger_log(logger, MP_LOG_LEVEL_INFO, "benchmark-message", "mode=threshold");
        result.attempted++;
        if (status == MP_LOG_STATUS_OK) {
            result.accepted++;
        } else if (status == MP_LOG_STATUS_BUSY) {
            result.busy++;
        } else if (status == MP_LOG_STATUS_QUEUE_FULL) {
            result.full++;
        } else {
            result.other++;
        }
        scheduled_nanos += interval_nanos;
    }

    benchmark_flush_logger_or_die(logger);
    if (mp_logger_get_stats(logger, &stats) == MP_LOG_STATUS_OK) {
        result.processed = stats.processed_records;
    }
    benchmark_destroy_logger(logger);
    result.elapsed_seconds = (double)duration_millis / 1000.0;
    return result;
}

/* Use binary search over paced rates to find the boundary between no-full and first-full behavior. */
static benchmark_run_result_t benchmark_find_threshold_case(
    size_t buffer_capacity,
    uint32_t sink_delay_micros,
    uint32_t duration_millis,
    uint32_t *out_no_full_rate,
    uint32_t *out_first_full_rate) {
    benchmark_run_result_t failing_result;
    benchmark_run_result_t probe_result;
    uint32_t low = 250u;
    uint32_t high = 4000u;
    uint32_t best_without_full = 0u;
    uint32_t first_with_full = 0u;

    memset(&failing_result, 0, sizeof(failing_result));
    while (low <= high) {
        uint32_t mid = low + ((high - low) / 2u);
        probe_result = benchmark_run_paced_rate_case(
            buffer_capacity,
            sink_delay_micros,
            mid,
            duration_millis);
        if (probe_result.full == 0u) {
            best_without_full = mid;
            low = mid + 1u;
        } else {
            first_with_full = mid;
            failing_result = probe_result;
            if (mid == 0u) {
                break;
            }
            high = mid - 1u;
        }
    }
    if (first_with_full == 0u) {
        first_with_full = low;
        failing_result = benchmark_run_paced_rate_case(
            buffer_capacity,
            sink_delay_micros,
            first_with_full,
            duration_millis);
    }
    *out_no_full_rate = best_without_full;
    *out_first_full_rate = first_with_full;
    return failing_result;
}

/* Fill a logger before startup to show the queue full point is deterministic for each profile. */
static void benchmark_run_prestart_fill_case(
    const benchmark_profile_t *profile,
    uint64_t *out_accepted_before_full,
    mp_log_status_t *out_failure_status) {
    mp_logger_config_t config;
    mp_logger_t *logger = NULL;
    uint64_t accepted = 0u;
    mp_log_status_t status = MP_LOG_STATUS_OK;

    mp_logger_config_init_defaults(&config);
    config.buffer_capacity = profile->buffer_capacity;
    config.message_capacity = profile->message_capacity;
    config.context_capacity = profile->context_capacity;
    (void)snprintf(config.active_streams, sizeof(config.active_streams), "%s", "");
    (void)snprintf(config.log_directory, sizeof(config.log_directory), "%s", ".tmp/benchmarks");
    (void)snprintf(config.file_name_prefix, sizeof(config.file_name_prefix), "%s", "bench");
    (void)snprintf(
        config.backup_file_name_prefix,
        sizeof(config.backup_file_name_prefix),
        "%s",
        "bench-prestart");

    if (mp_logger_create(&config, &logger) != MP_LOG_STATUS_OK) {
        fprintf(stderr, "failed to create prestart logger\n");
        exit(1);
    }

    for (;;) {
        status = mp_logger_log(logger, MP_LOG_LEVEL_INFO, "benchmark-message", "mode=prestart");
        if (status != MP_LOG_STATUS_OK) {
            break;
        }
        accepted++;
    }

    *out_accepted_before_full = accepted;
    *out_failure_status = status;
    mp_logger_destroy(logger);
}

/* Print the host metadata that makes the benchmark tables interpretable when copied into docs. */
static void benchmark_print_environment(void) {
    struct utsname system_name;
    long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    time_t now_seconds = time(NULL);
    struct tm now_utc;
    char timestamp[64];
    char cpu_model[160];
    FILE *cpuinfo = NULL;

    memset(&system_name, 0, sizeof(system_name));
    memset(&now_utc, 0, sizeof(now_utc));
    memset(cpu_model, 0, sizeof(cpu_model));
    if (uname(&system_name) != 0) {
        (void)snprintf(system_name.sysname, sizeof(system_name.sysname), "%s", "unknown");
        (void)snprintf(system_name.release, sizeof(system_name.release), "%s", "unknown");
        (void)snprintf(system_name.machine, sizeof(system_name.machine), "%s", "unknown");
    }
    if (gmtime_r(&now_seconds, &now_utc) == NULL ||
        strftime(timestamp, sizeof(timestamp), MP_LOGGER_BENCHMARK_CAPTURED_FORMAT, &now_utc) == 0) {
        (void)snprintf(timestamp, sizeof(timestamp), "%s", "unknown");
    }
    cpuinfo = fopen("/proc/cpuinfo", "r");
    if (cpuinfo != NULL) {
        char line[256];
        while (fgets(line, sizeof(line), cpuinfo) != NULL) {
            char *separator = NULL;
            if (strncmp(line, "model name", 10) != 0) {
                continue;
            }
            separator = strchr(line, ':');
            if (separator == NULL) {
                break;
            }
            separator += 1;
            while (*separator == ' ' || *separator == '\t') {
                separator++;
            }
            mp_logger_copy_truncated(cpu_model, sizeof(cpu_model), separator, NULL);
            {
                size_t length = strlen(cpu_model);
                while (length > 0u && (cpu_model[length - 1u] == '\n' || cpu_model[length - 1u] == '\r')) {
                    cpu_model[length - 1u] = '\0';
                    length--;
                }
            }
            break;
        }
        (void)fclose(cpuinfo);
    }
    if (cpu_model[0] == '\0') {
        (void)snprintf(cpu_model, sizeof(cpu_model), "%s", "unknown");
    }

    printf("# mp_logger Benchmark Results\n\n");
    printf("- Captured: `%s`\n", timestamp);
    printf("- Host: `%s %s %s`\n", system_name.sysname, system_name.release, system_name.machine);
    printf("- CPU: `%s`\n", cpu_model);
    printf("- Online CPUs: `%ld`\n", cpu_count > 0 ? cpu_count : 0);
    printf("- Build profile: `Release`\n\n");
}

/* Compare file throughput as sequential worker fanout increases from one stream to many. */
static void benchmark_print_file_stream_table(void) {
    const size_t stream_counts[] = {1u, 2u, 4u, 8u};
    size_t index = 0u;

    printf("## File Stream Throughput\n\n");
    printf(
        "Typical payload in this benchmark: message=`%zu bytes`, context=`%zu bytes`, default JSON formatting enabled.\n\n",
        strlen(benchmark_message_text),
        strlen(benchmark_context_text));
    printf("| Active file streams | Attempted | Processed to file | Processed logs/s | File write ops/s | Busy drops | Full drops |\n");
    printf("|--------------------:|----------:|------------------:|-----------------:|-----------------:|-----------:|-----------:|\n");
    for (index = 0u; index < (sizeof(stream_counts) / sizeof(stream_counts[0])); index++) {
        benchmark_run_result_t result = benchmark_run_single_thread_file_case(stream_counts[index], 1500u);
        double processed_rate = result.elapsed_seconds > 0.0
            ? (double)result.processed / result.elapsed_seconds
            : 0.0;
        double write_rate = processed_rate * (double)stream_counts[index];
        printf(
            "| %zu | %llu | %llu | %.0f | %.0f | %llu | %llu |\n",
            stream_counts[index],
            (unsigned long long)result.attempted,
            (unsigned long long)result.processed,
            processed_rate,
            write_rate,
            (unsigned long long)result.busy,
            (unsigned long long)result.full);
    }
    printf("\n");
    printf(
        "The logger supports up to `%u` active streams, but the worker writes them sequentially rather than in parallel.\n\n",
        MP_LOGGER_MAX_STREAMS);
}

/* Report reserved queue and worker scratch memory for each documented capacity profile. */
static void benchmark_print_buffer_table(void) {
    size_t index = 0;
    printf("## Buffer Footprint\n\n");
    printf("| Profile | Slots | Message cap | Context cap | Queue reserved bytes | Worker scratch bytes | Total reserved bytes |\n");
    printf("|:--------|------:|------------:|------------:|---------------------:|---------------------:|---------------------:|\n");
    for (index = 0; index < (sizeof(benchmark_profiles) / sizeof(benchmark_profiles[0])); index++) {
        const benchmark_profile_t *profile = &benchmark_profiles[index];
        size_t queue_bytes = benchmark_queue_reserved_bytes(profile);
        size_t worker_bytes = benchmark_worker_reserved_bytes(profile);
        printf(
            "| %s | %zu | %zu | %zu | %zu | %zu | %zu |\n",
            profile->name,
            profile->buffer_capacity,
            profile->message_capacity,
            profile->context_capacity,
            queue_bytes,
            worker_bytes,
            queue_bytes + worker_bytes);
    }
    printf("\n");
}

/* Summarize the exact enqueue call where each profile first reports queue saturation before startup. */
static void benchmark_print_prestart_table(void) {
    size_t index = 0;
    printf("## Deterministic Queue Full Point\n\n");
    printf("| Profile | Accepted before first failure | Failure status | Queue becomes full on call |\n");
    printf("|:--------|------------------------------:|:---------------|---------------------------:|\n");
    for (index = 0; index < (sizeof(benchmark_profiles) / sizeof(benchmark_profiles[0])); index++) {
        uint64_t accepted = 0u;
        mp_log_status_t failure_status = MP_LOG_STATUS_OK;
        benchmark_run_prestart_fill_case(&benchmark_profiles[index], &accepted, &failure_status);
        printf(
            "| %s | %llu | %s | %llu |\n",
            benchmark_profiles[index].name,
            (unsigned long long)accepted,
            mp_log_status_name(failure_status),
            (unsigned long long)(accepted + 1u));
    }
    printf("\n");
}

/* Print the overflow boundary for a deliberately slow sink using the paced-rate search helper. */
static void benchmark_print_threshold_table(void) {
    benchmark_run_result_t failing_result;
    uint32_t safe_rate = 0u;
    uint32_t failing_rate = 0u;

    failing_result = benchmark_find_threshold_case(128u, 500u, 2000u, &safe_rate, &failing_rate);
    printf("## Overflow Threshold With A 500us Sink Delay\n\n");
    printf("| Buffer slots | Highest rate without `QUEUE_FULL` | First rate with `QUEUE_FULL` | Busy drops at first full rate | Full drops at first full rate |\n");
    printf("|-------------:|----------------------------------:|------------------------------:|------------------------------:|------------------------------:|\n");
    printf(
        "| 128 | %u logs/s | %u logs/s | %llu | %llu |\n\n",
        safe_rate,
        failing_rate,
        (unsigned long long)failing_result.busy,
        (unsigned long long)failing_result.full);
}

/* Show how accepted throughput and drop modes change as producer concurrency rises. */
static void benchmark_print_concurrency_table(void) {
    size_t index = 0;
    const size_t thread_counts[] = {1u, 2u, 4u, 8u, 16u};

    printf("## Concurrent Producer Stress\n\n");
    printf("| Producer threads | Attempted | Accepted | Accepted logs/s | Busy drops | Full drops |\n");
    printf("|-----------------:|----------:|---------:|----------------:|-----------:|-----------:|\n");
    for (index = 0; index < (sizeof(thread_counts) / sizeof(thread_counts[0])); index++) {
        benchmark_run_result_t result = benchmark_run_concurrency_case(thread_counts[index], 1500u);
        double accepted_rate = result.elapsed_seconds > 0.0
            ? (double)result.accepted / result.elapsed_seconds
            : 0.0;
        printf(
            "| %zu | %llu | %llu | %.0f | %llu | %llu |\n",
            thread_counts[index],
            (unsigned long long)result.attempted,
            (unsigned long long)result.accepted,
            accepted_rate,
            (unsigned long long)result.busy,
            (unsigned long long)result.full);
    }
    printf("\n");
}

/* Close with the exact status mechanisms a reader should expect to see during overflow. */
static void benchmark_print_overflow_summary(void) {
    printf("## Overflow Mechanisms Observed\n\n");
    printf("- `mp_logger_log()` returns `MP_LOG_STATUS_BUSY` when `pthread_mutex_trylock()` cannot acquire the queue mutex.\n");
    printf("- `mp_logger_log()` returns `MP_LOG_STATUS_QUEUE_FULL` when `queue_count` reaches `buffer_capacity`.\n");
    printf("- `mp_logger_get_stats()` exposes cumulative `dropped_busy` and `dropped_full` totals.\n");
    printf("- The worker mirrors both drop modes to the backup logger as warning lines once counters increase.\n");
}

int main(void) {
    benchmark_ensure_directory(".tmp");
    benchmark_ensure_directory(".tmp/benchmarks");
    benchmark_ensure_directory(".tmp/benchmarks/file-streams");
    benchmark_print_environment();
    benchmark_print_file_stream_table();
    benchmark_print_buffer_table();
    benchmark_print_prestart_table();
    benchmark_print_threshold_table();
    benchmark_print_concurrency_table();
    benchmark_print_overflow_summary();
    return 0;
}
