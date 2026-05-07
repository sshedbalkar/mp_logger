#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "mp_logger_internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int64_t mp_logger_now_monotonic_millis(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return (int64_t)now.tv_sec * 1000 + (int64_t)(now.tv_nsec / 1000000);
}

static void mp_logger_make_run_suffix(char *buffer, size_t buffer_capacity) {
    struct tm utc_time;
    time_t now_seconds = (time_t)(mp_logger_now_millis() / 1000);
    if (buffer == NULL || buffer_capacity == 0) {
        return;
    }
    memset(&utc_time, 0, sizeof(utc_time));
    if (gmtime_r(&now_seconds, &utc_time) == NULL) {
        (void)snprintf(buffer, buffer_capacity, "19700101T000000Z");
        return;
    }
    if (strftime(buffer, buffer_capacity, "%Y%m%dT%H%M%SZ", &utc_time) == 0) {
        (void)snprintf(buffer, buffer_capacity, "19700101T000000Z");
    }
}

/*
 * Mirror newly observed drop counters into the backup logger so contention and saturation remain
 * visible even when the primary queue is under pressure.
 */
static void mp_logger_flush_drop_counters(
    mp_logger_t *logger,
    uint64_t *last_busy_total,
    uint64_t *last_full_total) {
    uint64_t current_busy = 0;
    uint64_t current_full = 0;
    char message[128];
    if (logger == NULL || last_busy_total == NULL || last_full_total == NULL) {
        return;
    }

    current_busy = atomic_load(&logger->dropped_busy_total);
    if (current_busy > *last_busy_total) {
        (void)snprintf(
            message,
            sizeof(message),
            "log call contention dropped records total=%llu",
            (unsigned long long)current_busy);
        mp_logger_backup_write(logger, "WARNING", message);
        *last_busy_total = current_busy;
    }

    current_full = atomic_load(&logger->dropped_full_total);
    if (current_full > *last_full_total) {
        (void)snprintf(
            message,
            sizeof(message),
            "buffer saturation dropped records total=%llu",
            (unsigned long long)current_full);
        mp_logger_backup_write(logger, "WARNING", message);
        *last_full_total = current_full;
    }
}

/*
 * Dequeue into thread-local scratch buffers before invoking streams so callbacks never observe
 * queue storage that is about to be reused by producers.
 */
static void *mp_logger_worker_main(void *context) {
    mp_logger_t *logger = (mp_logger_t *)context;
    char *message_buffer = NULL;
    char *context_buffer = NULL;
    char *render_buffer = NULL;
    uint64_t last_busy_total = 0;
    uint64_t last_full_total = 0;

    if (logger == NULL) {
        return NULL;
    }

    message_buffer = (char *)calloc(logger->config.message_capacity, 1u);
    context_buffer = (char *)calloc(logger->config.context_capacity, 1u);
    render_buffer = (char *)calloc(logger->render_capacity, 1u);
    if (message_buffer == NULL || context_buffer == NULL || render_buffer == NULL) {
        mp_logger_backup_write(logger, "ERROR", "worker thread could not allocate local buffers");
        free(message_buffer);
        free(context_buffer);
        free(render_buffer);
        return NULL;
    }

    for (;;) {
        mp_log_record_t record;
        int have_record = 0;

        memset(&record, 0, sizeof(record));
        (void)pthread_mutex_lock(&logger->queue_mutex);
        while (logger->queue_count == 0 && !logger->shutdown_requested) {
            (void)pthread_cond_wait(&logger->queue_cond, &logger->queue_mutex);
        }
        if (logger->queue_count > 0) {
            mp_log_slot_t *slot = &logger->slots[logger->queue_head];
            memcpy(message_buffer, slot->message_buffer, slot->message_length + 1u);
            memcpy(context_buffer, slot->context_buffer, slot->context_length + 1u);
            record.sequence_id = slot->sequence_id;
            record.unix_epoch_millis = slot->unix_epoch_millis;
            record.level = slot->level;
            record.message = message_buffer;
            record.context_text = context_buffer;
            logger->queue_head = (logger->queue_head + 1u) % logger->config.buffer_capacity;
            logger->queue_count--;
            have_record = 1;
        } else if (logger->shutdown_requested) {
            (void)pthread_mutex_unlock(&logger->queue_mutex);
            break;
        }
        (void)pthread_mutex_unlock(&logger->queue_mutex);

        mp_logger_flush_drop_counters(logger, &last_busy_total, &last_full_total);
        if (!have_record) {
            continue;
        }

        {
            size_t index = 0;
            size_t rendered_length = mp_logger_render_record(
                logger,
                &record,
                render_buffer,
                logger->render_capacity);
            (void)pthread_mutex_lock(&logger->stream_mutex);
            for (index = 0; index < logger->stream_count; index++) {
                mp_logger_stream_t *stream = &logger->streams[index];
                mp_log_status_t status = MP_LOG_STATUS_OK;
                if (!mp_logger_level_in_range(record.level, stream->minimum_level, stream->maximum_level)) {
                    continue;
                }
                status = stream->write(
                    stream->stream_context,
                    &record,
                    render_buffer,
                    rendered_length);
                if (status != MP_LOG_STATUS_OK) {
                    char message[160];
                    (void)snprintf(
                        message,
                        sizeof(message),
                        "stream=%s write failure status=%s",
                        stream->stream_name,
                        mp_log_status_name(status));
                    mp_logger_backup_write(logger, "ERROR", message);
                }
            }
            (void)pthread_mutex_unlock(&logger->stream_mutex);
        }

        atomic_fetch_add(&logger->processed_records_total, 1u);
    }

    mp_logger_flush_drop_counters(logger, &last_busy_total, &last_full_total);
    free(message_buffer);
    free(context_buffer);
    free(render_buffer);
    return NULL;
}

static int mp_logger_validate_config(const mp_logger_config_t *config) {
    if (config == NULL) {
        return 0;
    }
    return config->buffer_capacity > 0 &&
        config->message_capacity > 1 &&
        config->context_capacity > 1 &&
        config->service_name[0] != '\0' &&
        config->environment_name[0] != '\0' &&
        config->file_name_prefix[0] != '\0' &&
        config->backup_file_name_prefix[0] != '\0' &&
        config->log_directory[0] != '\0';
}

/* Allocate one contiguous queue slot array plus per-slot message and context backing storage. */
static mp_log_status_t mp_logger_allocate_buffers(mp_logger_t *logger) {
    size_t index = 0;
    logger->slots = (mp_log_slot_t *)calloc(logger->config.buffer_capacity, sizeof(mp_log_slot_t));
    logger->message_storage = (char *)calloc(
        logger->config.buffer_capacity * logger->config.message_capacity,
        1u);
    logger->context_storage = (char *)calloc(
        logger->config.buffer_capacity * logger->config.context_capacity,
        1u);
    if (logger->slots == NULL || logger->message_storage == NULL || logger->context_storage == NULL) {
        return MP_LOG_STATUS_IO_ERROR;
    }
    for (index = 0; index < logger->config.buffer_capacity; index++) {
        logger->slots[index].message_buffer =
            logger->message_storage + (index * logger->config.message_capacity);
        logger->slots[index].context_buffer =
            logger->context_storage + (index * logger->config.context_capacity);
    }
    return MP_LOG_STATUS_OK;
}

static uint_fast32_t mp_logger_level_range_mask(
    mp_log_level_t minimum_level,
    mp_log_level_t maximum_level) {
    uint_fast32_t mask = 0u;
    int level = 0;
    if (minimum_level < MP_LOG_LEVEL_TRACE ||
        maximum_level > MP_LOG_LEVEL_FATAL ||
        minimum_level > maximum_level) {
        return 0u;
    }
    for (level = (int)minimum_level; level <= (int)maximum_level; level++) {
        mask |= ((uint_fast32_t)1u << (unsigned int)level);
    }
    return mask;
}

/* Reject duplicate stream names up front so sink fanout remains deterministic. */
mp_log_status_t mp_logger_add_owned_stream(mp_logger_t *logger, const mp_logger_stream_t *stream) {
    size_t index = 0;
    if (logger == NULL || stream == NULL || stream->write == NULL || stream->stream_name[0] == '\0') {
        return MP_LOG_STATUS_INVALID_ARGUMENT;
    }
    (void)pthread_mutex_lock(&logger->stream_mutex);
    if (logger->stream_count >= MP_LOGGER_MAX_STREAMS) {
        (void)pthread_mutex_unlock(&logger->stream_mutex);
        return MP_LOG_STATUS_LIMIT_EXCEEDED;
    }
    for (index = 0; index < logger->stream_count; index++) {
        if (strcmp(logger->streams[index].stream_name, stream->stream_name) == 0) {
            (void)pthread_mutex_unlock(&logger->stream_mutex);
            return MP_LOG_STATUS_CONFIG_ERROR;
        }
    }
    logger->streams[logger->stream_count] = *stream;
    logger->stream_count++;
    atomic_fetch_or(
        &logger->enabled_level_mask,
        mp_logger_level_range_mask(stream->minimum_level, stream->maximum_level));
    (void)pthread_mutex_unlock(&logger->stream_mutex);
    return MP_LOG_STATUS_OK;
}

const char *mp_log_level_name(mp_log_level_t level) {
    switch (level) {
    case MP_LOG_LEVEL_TRACE:
        return "TRACE";
    case MP_LOG_LEVEL_DEBUG:
        return "DEBUG";
    case MP_LOG_LEVEL_INFO:
        return "INFO";
    case MP_LOG_LEVEL_WARNING:
        return "WARNING";
    case MP_LOG_LEVEL_ERROR:
        return "ERROR";
    case MP_LOG_LEVEL_FATAL:
        return "FATAL";
    default:
        return "UNKNOWN";
    }
}

const char *mp_log_status_name(mp_log_status_t status) {
    switch (status) {
    case MP_LOG_STATUS_OK:
        return "OK";
    case MP_LOG_STATUS_QUEUE_FULL:
        return "QUEUE_FULL";
    case MP_LOG_STATUS_BUSY:
        return "BUSY";
    case MP_LOG_STATUS_INVALID_ARGUMENT:
        return "INVALID_ARGUMENT";
    case MP_LOG_STATUS_IO_ERROR:
        return "IO_ERROR";
    case MP_LOG_STATUS_CONFIG_ERROR:
        return "CONFIG_ERROR";
    case MP_LOG_STATUS_NOT_RUNNING:
        return "NOT_RUNNING";
    case MP_LOG_STATUS_INTERNAL_ERROR:
        return "INTERNAL_ERROR";
    case MP_LOG_STATUS_LIMIT_EXCEEDED:
        return "LIMIT_EXCEEDED";
    default:
        return "UNKNOWN";
    }
}

/*
 * Normalize file-safe prefixes and fully construct the logger before any handle escapes.
 * Failures unwind through mp_logger_destroy() so partial allocation cleanup stays in one place.
 */
mp_log_status_t mp_logger_create(const mp_logger_config_t *config, mp_logger_t **out_logger) {
    mp_logger_t *logger = NULL;
    mp_log_status_t status = MP_LOG_STATUS_OK;
    mp_logger_config_t normalized_config;

    if (config == NULL || out_logger == NULL) {
        return MP_LOG_STATUS_INVALID_ARGUMENT;
    }
    if (!mp_logger_validate_config(config)) {
        return MP_LOG_STATUS_CONFIG_ERROR;
    }

    normalized_config = *config;
    mp_logger_sanitize_file_component(normalized_config.file_name_prefix);
    mp_logger_sanitize_file_component(normalized_config.backup_file_name_prefix);

    logger = (mp_logger_t *)calloc(1u, sizeof(*logger));
    if (logger == NULL) {
        return MP_LOG_STATUS_IO_ERROR;
    }

    logger->config = normalized_config;
    logger->backup_logger.backup_fd = -1;
    logger->render_capacity =
        normalized_config.message_capacity +
        normalized_config.context_capacity +
        MP_LOGGER_RENDER_PADDING;
    mp_logger_make_run_suffix(logger->run_suffix, sizeof(logger->run_suffix));
    atomic_init(&logger->queued_records_total, 0u);
    atomic_init(&logger->processed_records_total, 0u);
    atomic_init(&logger->dropped_busy_total, 0u);
    atomic_init(&logger->dropped_full_total, 0u);
    atomic_init(&logger->enabled_level_mask, 0u);

    (void)pthread_mutex_init(&logger->queue_mutex, NULL);
    (void)pthread_mutex_init(&logger->stream_mutex, NULL);
    (void)pthread_mutex_init(&logger->backup_logger.mutex, NULL);
    (void)pthread_cond_init(&logger->queue_cond, NULL);

    status = mp_logger_allocate_buffers(logger);
    if (status != MP_LOG_STATUS_OK) {
        mp_logger_destroy(logger);
        return status;
    }

    status = mp_logger_backup_open(logger);
    if (status != MP_LOG_STATUS_OK) {
        mp_logger_destroy(logger);
        return status;
    }

    status = mp_logger_build_builtin_streams(logger);
    if (status != MP_LOG_STATUS_OK) {
        mp_logger_destroy(logger);
        return status;
    }

    *out_logger = logger;
    return MP_LOG_STATUS_OK;
}

/* Bootstrap creation is all-or-nothing: config load, allocation, and worker start must all succeed. */
mp_log_status_t mp_logger_create_from_bootstrap(const char *config_path, mp_logger_t **out_logger) {
    mp_logger_config_t config;
    mp_logger_t *logger = NULL;
    mp_log_status_t status = MP_LOG_STATUS_OK;
    status = mp_logger_bootstrap_load(config_path, &config);
    if (status != MP_LOG_STATUS_OK) {
        return status;
    }
    status = mp_logger_create(&config, &logger);
    if (status != MP_LOG_STATUS_OK) {
        return status;
    }
    status = mp_logger_start(logger);
    if (status != MP_LOG_STATUS_OK) {
        mp_logger_destroy(logger);
        return status;
    }
    *out_logger = logger;
    return MP_LOG_STATUS_OK;
}

mp_log_status_t mp_logger_add_stream(mp_logger_t *logger, const mp_logger_stream_t *stream) {
    return mp_logger_add_owned_stream(logger, stream);
}

mp_log_status_t mp_logger_start(mp_logger_t *logger) {
    if (logger == NULL) {
        return MP_LOG_STATUS_INVALID_ARGUMENT;
    }
    if (logger->worker_started) {
        return MP_LOG_STATUS_OK;
    }
    if (logger->stream_count == 0) {
        return MP_LOG_STATUS_CONFIG_ERROR;
    }
    logger->shutdown_requested = 0;
    if (pthread_create(&logger->worker_thread, NULL, mp_logger_worker_main, logger) != 0) {
        return MP_LOG_STATUS_INTERNAL_ERROR;
    }
    logger->worker_started = 1;
    return MP_LOG_STATUS_OK;
}

/*
 * The producer path uses trylock so callers never block behind worker activity or slow sinks.
 * Once admitted, the record is copied into preallocated slot storage and the worker is signaled.
 */
mp_log_status_t mp_logger_log(
    mp_logger_t *logger,
    mp_log_level_t level,
    const char *message,
    const char *context_text) {
    mp_log_slot_t *slot = NULL;
    size_t ignored_length = 0;
    if (logger == NULL || message == NULL) {
        return MP_LOG_STATUS_INVALID_ARGUMENT;
    }
    if (logger->shutdown_requested) {
        return MP_LOG_STATUS_NOT_RUNNING;
    }
    if (pthread_mutex_trylock(&logger->queue_mutex) != 0) {
        atomic_fetch_add(&logger->dropped_busy_total, 1u);
        return MP_LOG_STATUS_BUSY;
    }
    if (logger->queue_count >= logger->config.buffer_capacity) {
        (void)pthread_mutex_unlock(&logger->queue_mutex);
        atomic_fetch_add(&logger->dropped_full_total, 1u);
        return MP_LOG_STATUS_QUEUE_FULL;
    }

    slot = &logger->slots[logger->queue_tail];
    slot->sequence_id = atomic_fetch_add(&logger->queued_records_total, 1u) + 1u;
    slot->unix_epoch_millis = mp_logger_now_millis();
    slot->level = level;
    mp_logger_copy_truncated(
        slot->message_buffer,
        logger->config.message_capacity,
        message,
        &slot->message_length);
    mp_logger_copy_truncated(
        slot->context_buffer,
        logger->config.context_capacity,
        context_text == NULL ? "" : context_text,
        &slot->context_length);
    if (message != NULL && strlen(message) >= logger->config.message_capacity) {
        ignored_length++;
    }
    if (context_text != NULL && strlen(context_text) >= logger->config.context_capacity) {
        ignored_length++;
    }
    logger->queue_tail = (logger->queue_tail + 1u) % logger->config.buffer_capacity;
    logger->queue_count++;
    (void)pthread_mutex_unlock(&logger->queue_mutex);
    (void)pthread_cond_signal(&logger->queue_cond);
    (void)ignored_length;
    return MP_LOG_STATUS_OK;
}

/*
 * Read the cached stream-level bitmask so callers can cheaply skip work for disabled levels
 * without contending with worker-side sink dispatch.
 */
bool mp_logger_is_level_enabled(const mp_logger_t *logger, mp_log_level_t level) {
    if (logger == NULL ||
        logger->shutdown_requested ||
        level < MP_LOG_LEVEL_TRACE ||
        level > MP_LOG_LEVEL_FATAL) {
        return false;
    }
    return (atomic_load(&logger->enabled_level_mask) & ((uint_fast32_t)1u << (unsigned int)level)) != 0u;
}

/* Wait for the processed counter to catch up with the queued counter snapshot taken at entry. */
mp_log_status_t mp_logger_flush(mp_logger_t *logger, uint32_t timeout_millis) {
    int64_t deadline_millis = 0;
    uint64_t target_processed = 0;
    if (logger == NULL) {
        return MP_LOG_STATUS_INVALID_ARGUMENT;
    }
    if (!logger->worker_started) {
        return logger->queue_count == 0 ? MP_LOG_STATUS_OK : MP_LOG_STATUS_NOT_RUNNING;
    }
    target_processed = atomic_load(&logger->queued_records_total);
    deadline_millis = mp_logger_now_monotonic_millis() + (int64_t)timeout_millis;
    while (atomic_load(&logger->processed_records_total) < target_processed) {
        struct timespec wait_time;
        if (timeout_millis > 0 && mp_logger_now_monotonic_millis() > deadline_millis) {
            return MP_LOG_STATUS_INTERNAL_ERROR;
        }
        wait_time.tv_sec = 0;
        wait_time.tv_nsec = 1000000L;
        nanosleep(&wait_time, NULL);
    }
    return MP_LOG_STATUS_OK;
}

mp_log_status_t mp_logger_get_stats(const mp_logger_t *logger, mp_logger_stats_t *out_stats) {
    if (logger == NULL || out_stats == NULL) {
        return MP_LOG_STATUS_INVALID_ARGUMENT;
    }
    out_stats->queued_records = atomic_load(&logger->queued_records_total);
    out_stats->processed_records = atomic_load(&logger->processed_records_total);
    out_stats->dropped_busy = atomic_load(&logger->dropped_busy_total);
    out_stats->dropped_full = atomic_load(&logger->dropped_full_total);
    out_stats->active_stream_count = logger->stream_count;
    return MP_LOG_STATUS_OK;
}

/*
 * Signal shutdown first so the worker stops waiting for new records, then join with the platform
 * timeout path when available.
 */
mp_log_status_t mp_logger_shutdown(mp_logger_t *logger, uint32_t timeout_millis) {
    if (logger == NULL) {
        return MP_LOG_STATUS_INVALID_ARGUMENT;
    }
    logger->shutdown_requested = 1;
    (void)pthread_cond_signal(&logger->queue_cond);
    if (!logger->worker_started) {
        return MP_LOG_STATUS_OK;
    }
#ifdef __linux__
    if (timeout_millis > 0) {
        struct timespec deadline;
        int join_status = 0;
        if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) {
            return MP_LOG_STATUS_INTERNAL_ERROR;
        }
        deadline.tv_sec += (time_t)(timeout_millis / 1000u);
        deadline.tv_nsec += (long)((timeout_millis % 1000u) * 1000000u);
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec += 1;
            deadline.tv_nsec -= 1000000000L;
        }
        join_status = pthread_timedjoin_np(logger->worker_thread, NULL, &deadline);
        if (join_status != 0) {
            return MP_LOG_STATUS_INTERNAL_ERROR;
        }
    } else {
        if (pthread_join(logger->worker_thread, NULL) != 0) {
            return MP_LOG_STATUS_INTERNAL_ERROR;
        }
    }
#else
    (void)timeout_millis;
    if (pthread_join(logger->worker_thread, NULL) != 0) {
        return MP_LOG_STATUS_INTERNAL_ERROR;
    }
#endif
    logger->worker_started = 0;
    return MP_LOG_STATUS_OK;
}

/* Destroy runs the full teardown sequence even when callers forgot to stop the worker first. */
void mp_logger_destroy(mp_logger_t *logger) {
    if (logger == NULL) {
        return;
    }
    if (logger->worker_started) {
        (void)mp_logger_shutdown(logger, 5000u);
    }
    mp_logger_destroy_streams(logger);
    mp_logger_backup_close(logger);
    (void)pthread_cond_destroy(&logger->queue_cond);
    (void)pthread_mutex_destroy(&logger->backup_logger.mutex);
    (void)pthread_mutex_destroy(&logger->stream_mutex);
    (void)pthread_mutex_destroy(&logger->queue_mutex);
    free(logger->slots);
    free(logger->message_storage);
    free(logger->context_storage);
    free(logger);
}
