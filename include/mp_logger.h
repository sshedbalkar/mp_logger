#ifndef MP_LOGGER_H
#define MP_LOGGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mp_logger_constants.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Describes the severity assigned to a record before routing it to sinks. */
typedef enum {
    MP_LOG_LEVEL_TRACE = 0,
    MP_LOG_LEVEL_DEBUG = 1,
    MP_LOG_LEVEL_INFO = 2,
    MP_LOG_LEVEL_WARNING = 3,
    MP_LOG_LEVEL_ERROR = 4,
    MP_LOG_LEVEL_FATAL = 5
} mp_log_level_t;

/* Selects how the worker renders each record before it hands the line to sinks. */
typedef enum {
    MP_LOG_FORMAT_TEXT = 0,
    MP_LOG_FORMAT_JSON = 1
} mp_log_format_t;

/* Reports the outcome of logger lifecycle, config, and enqueue operations. */
typedef enum {
    MP_LOG_STATUS_OK = 0,
    MP_LOG_STATUS_QUEUE_FULL = 1,
    MP_LOG_STATUS_BUSY = 2,
    MP_LOG_STATUS_INVALID_ARGUMENT = 3,
    MP_LOG_STATUS_IO_ERROR = 4,
    MP_LOG_STATUS_CONFIG_ERROR = 5,
    MP_LOG_STATUS_NOT_RUNNING = 6,
    MP_LOG_STATUS_INTERNAL_ERROR = 7,
    MP_LOG_STATUS_LIMIT_EXCEEDED = 8
} mp_log_status_t;

typedef struct mp_logger mp_logger_t;

/* Identifies the stored value type for one structured log field. */
typedef enum {
    MP_LOG_FIELD_STRING = 0,
    MP_LOG_FIELD_BOOL = 1,
    MP_LOG_FIELD_INT64 = 2,
    MP_LOG_FIELD_UINT64 = 3,
    MP_LOG_FIELD_FLOAT64 = 4
} mp_log_field_type_t;

/* Represents one structured field attached to a record in the order it was supplied. */
typedef struct {
    const char *key;
    mp_log_field_type_t type;
    union {
        const char *string_value;
        bool bool_value;
        int64_t int64_value;
        uint64_t uint64_value;
        double float64_value;
    } value;
} mp_log_field_t;

/*
 * Carries the structured fields that every stream callback receives for a dequeued record.
 * The pointers remain valid only for the duration of the callback that receives them.
 */
typedef struct {
    uint64_t sequence_id;
    int64_t unix_epoch_millis;
    mp_log_level_t level;
    const char *message;
    const char *context_text;
    const mp_log_field_t *fields;
    size_t field_count;
} mp_log_record_t;

/*
 * Configures queue sizing, rendering, built-in sinks, and output locations for a logger.
 * Call mp_logger_config_init_defaults() first, then override only the fields your application
 * needs to customize.
 */
typedef struct {
    char service_name[MP_LOGGER_NAME_CAPACITY];
    char environment_name[MP_LOGGER_NAME_CAPACITY];
    char build_version[MP_LOGGER_NAME_CAPACITY];
    size_t buffer_capacity;
    size_t message_capacity;
    size_t context_capacity;
    size_t field_capacity;
    size_t field_key_capacity;
    size_t field_value_capacity;
    mp_log_format_t format;
    int pretty_output;
    char log_directory[MP_LOGGER_PATH_CAPACITY];
    char file_name_prefix[MP_LOGGER_NAME_CAPACITY];
    char backup_file_name_prefix[MP_LOGGER_NAME_CAPACITY];
    char active_streams[MP_LOGGER_ACTIVE_STREAMS_CAPACITY];
    mp_log_level_t stdout_min_level;
    mp_log_level_t stdout_max_level;
    mp_log_level_t stderr_min_level;
    mp_log_level_t stderr_max_level;
    mp_log_level_t file_min_level;
    mp_log_level_t file_max_level;
    mp_log_level_t udp_min_level;
    mp_log_level_t udp_max_level;
    char udp_host[MP_LOGGER_HOST_CAPACITY];
    uint16_t udp_port;
} mp_logger_config_t;

/*
 * Writes one rendered record to a custom sink.
 * The callback runs on the worker thread, so slow or blocking work here directly slows drain
 * throughput for every active sink.
 */
typedef mp_log_status_t (*mp_logger_stream_write_fn)(
    void *stream_context,
    const mp_log_record_t *record,
    const char *formatted_entry,
    size_t formatted_entry_length);

/* Releases stream_context when the logger removes or destroys a registered stream. */
typedef void (*mp_logger_stream_destroy_fn)(void *stream_context);

/*
 * Describes a custom sink that accepts records within a level range.
 * The logger copies this struct on registration and takes ownership of stream_context teardown
 * only when destroy is non-NULL.
 */
typedef struct {
    char stream_name[MP_LOGGER_NAME_CAPACITY];
    mp_log_level_t minimum_level;
    mp_log_level_t maximum_level;
    void *stream_context;
    mp_logger_stream_write_fn write;
    mp_logger_stream_destroy_fn destroy;
} mp_logger_stream_t;

/* Exposes cumulative queue and sink counts so callers can observe pressure and drops. */
typedef struct {
    uint64_t queued_records;
    uint64_t processed_records;
    uint64_t dropped_busy;
    uint64_t dropped_full;
    size_t active_stream_count;
} mp_logger_stats_t;

/* Build a string-valued structured field for mp_logger_log_fields(). */
mp_log_field_t mp_log_field_string(const char *key, const char *value);

/* Build a boolean structured field for mp_logger_log_fields(). */
mp_log_field_t mp_log_field_bool(const char *key, bool value);

/* Build a signed 64-bit structured field for mp_logger_log_fields(). */
mp_log_field_t mp_log_field_int64(const char *key, int64_t value);

/* Build an unsigned 64-bit structured field for mp_logger_log_fields(). */
mp_log_field_t mp_log_field_uint64(const char *key, uint64_t value);

/* Build a float64 structured field for mp_logger_log_fields(). */
mp_log_field_t mp_log_field_float64(const char *key, double value);

/*
 * Initialize every config field to the library defaults documented in README.md.
 * Callers typically use this before overriding capacities, stream selection, or metadata.
 */
void mp_logger_config_init_defaults(mp_logger_config_t *config);

/*
 * Apply one section/key/value override to an initialized config.
 * Use an empty section for service_name, environment_name, and build_version.
 */
mp_log_status_t mp_logger_config_apply_override(
    mp_logger_config_t *config,
    const char *section,
    const char *key,
    const char *value);

/*
 * Load a commented bootstrap YAML file into out_config.
 * Unknown sections, unknown keys, malformed values, and unreadable files are rejected instead
 * of being ignored.
 */
mp_log_status_t mp_logger_bootstrap_load(const char *config_path, mp_logger_config_t *out_config);

/*
 * Create a logger instance from a validated config without starting the worker thread.
 * Use this path when you want to register custom streams before log processing begins.
 */
mp_log_status_t mp_logger_create(const mp_logger_config_t *config, mp_logger_t **out_logger);

/*
 * Load config_path, create the logger, and start the worker thread in one call.
 * On failure, no partially started logger is returned to the caller.
 */
mp_log_status_t mp_logger_create_from_bootstrap(const char *config_path, mp_logger_t **out_logger);

/*
 * Register a custom stream on an existing logger.
 * Stream names must be unique, and the callback will run on the worker thread for matching
 * records once the logger is started.
 */
mp_log_status_t mp_logger_add_stream(mp_logger_t *logger, const mp_logger_stream_t *stream);

/*
 * Apply runtime-safe config changes to an existing logger.
 * Queue capacities, storage paths, active stream topology, and UDP endpoints are init-only.
 */
mp_log_status_t mp_logger_configure(mp_logger_t *logger, const mp_logger_config_t *config);

/* Copy the logger's current effective config into out_config. */
mp_log_status_t mp_logger_get_config(const mp_logger_t *logger, mp_logger_config_t *out_config);

/*
 * Start the worker thread that drains queued records to active streams.
 * Starting an already running logger is a no-op, but starting without any streams is rejected.
 */
mp_log_status_t mp_logger_start(mp_logger_t *logger);

/*
 * Attempt to enqueue one record without blocking on sink I/O.
 * The call can return BUSY or QUEUE_FULL when contention or saturation prevents admission, so
 * callers should treat the status as part of normal backpressure handling.
 */
mp_log_status_t mp_logger_log(
    mp_logger_t *logger,
    mp_log_level_t level,
    const char *message,
    const char *context_text);

/*
 * Attempt to enqueue one record plus ordered structured fields without blocking on sink I/O.
 * Field keys must be unique within the call and must not reuse the built-in output keys.
 */
mp_log_status_t mp_logger_log_fields(
    mp_logger_t *logger,
    mp_log_level_t level,
    const char *message,
    const char *context_text,
    const mp_log_field_t *fields,
    size_t field_count);

/*
 * Report whether the logger currently has any registered stream that accepts level.
 * Returns false for NULL loggers, invalid levels, and loggers that have been shut down.
 */
bool mp_logger_is_level_enabled(const mp_logger_t *logger, mp_log_level_t level);

/*
 * Wait until every record queued before the call has been processed or the timeout expires.
 * A zero timeout waits without a deadline.
 */
mp_log_status_t mp_logger_flush(mp_logger_t *logger, uint32_t timeout_millis);

/*
 * Snapshot the cumulative queue counters and active stream count into out_stats.
 * This is the supported way to observe drops and worker progress from outside the logger.
 */
mp_log_status_t mp_logger_get_stats(const mp_logger_t *logger, mp_logger_stats_t *out_stats);

/*
 * Request worker shutdown after queued records have been drained.
 * The timeout applies to the worker join; once shutdown succeeds the logger can be destroyed.
 */
mp_log_status_t mp_logger_shutdown(mp_logger_t *logger, uint32_t timeout_millis);

/* Destroy the logger and any owned stream resources. Safe to call on a stopped or running logger. */
void mp_logger_destroy(mp_logger_t *logger);

/* Return the stable uppercase name used when rendering a level value. */
const char *mp_log_level_name(mp_log_level_t level);

/* Return the stable uppercase name for a status code. */
const char *mp_log_status_name(mp_log_status_t status);

#ifdef __cplusplus
}
#endif

#endif
