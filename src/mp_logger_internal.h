#ifndef MP_LOGGER_INTERNAL_H
#define MP_LOGGER_INTERNAL_H

#include "mp_logger.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/socket.h>

#define MP_LOGGER_MAX_STREAMS 8u
#define MP_LOGGER_TIMESTAMP_CAPACITY 32u
#define MP_LOGGER_RENDER_PADDING 384u

/* Holds one queued record plus its owned copied message, context, and field storage. */
typedef struct {
    uint64_t sequence_id;
    int64_t unix_epoch_millis;
    mp_log_level_t level;
    size_t message_length;
    size_t context_length;
    size_t field_count;
    char *message_buffer;
    char *context_buffer;
    mp_log_field_t *fields;
    char *field_key_storage;
    char *field_string_storage;
} mp_log_slot_t;

/* Serializes backup warning output written outside the primary queue path. */
typedef struct {
    int backup_fd;
    char backup_path[MP_LOGGER_PATH_CAPACITY];
    pthread_mutex_t mutex;
} mp_backup_logger_t;

/* Owns one built-in file sink handle and its resolved output path. */
typedef struct {
    FILE *file_handle;
    char path[MP_LOGGER_PATH_CAPACITY];
    int close_on_destroy;
} mp_file_stream_state_t;

/* Owns one built-in UDP sink destination and its connected socket. */
typedef struct {
    int socket_fd;
    struct sockaddr_storage address;
    socklen_t address_length;
    char endpoint[MP_LOGGER_HOST_CAPACITY + 16u];
} mp_udp_stream_state_t;

/* Stores the full logger runtime state, queue buffers, streams, and worker coordination. */
struct mp_logger {
    mp_logger_config_t config;
    char run_suffix[32];
    mp_log_slot_t *slots;
    char *message_storage;
    char *context_storage;
    mp_log_field_t *field_storage;
    char *field_key_storage;
    char *field_string_storage;
    size_t queue_head;
    size_t queue_tail;
    size_t queue_count;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    pthread_mutex_t stream_mutex;
    pthread_t worker_thread;
    int worker_started;
    int shutdown_requested;
    mp_logger_stream_t streams[MP_LOGGER_MAX_STREAMS];
    size_t stream_count;
    size_t render_capacity;
    mp_backup_logger_t backup_logger;
    atomic_uint_fast64_t queued_records_total;
    atomic_uint_fast64_t processed_records_total;
    atomic_uint_fast64_t dropped_busy_total;
    atomic_uint_fast64_t dropped_full_total;
    atomic_uint_fast32_t enabled_level_mask;
};

/* Return the current wall-clock time in UTC milliseconds for rendered records. */
int64_t mp_logger_now_millis(void);

/* Format unix_epoch_millis into the logger's fixed UTC timestamp representation. */
void mp_logger_format_timestamp(int64_t unix_epoch_millis, char *buffer, size_t buffer_capacity);

/* Render record into buffer according to logger->config.format and return the byte count. */
size_t mp_logger_render_record(
    const mp_logger_t *logger,
    const mp_log_record_t *record,
    char *buffer,
    size_t buffer_capacity);

/* Open the backup warning logger before stream initialization or worker startup. */
mp_log_status_t mp_logger_backup_open(mp_logger_t *logger);

/* Close the backup warning logger if it is currently open. */
void mp_logger_backup_close(mp_logger_t *logger);

/* Write one best-effort warning line to the backup logger. */
void mp_logger_backup_write(mp_logger_t *logger, const char *severity, const char *message);

/* Materialize the built-in stdout, stderr, file, and UDP streams requested by config. */
mp_log_status_t mp_logger_build_builtin_streams(mp_logger_t *logger);

/* Copy stream into the owned stream table and take destroy responsibility when present. */
mp_log_status_t mp_logger_add_owned_stream(mp_logger_t *logger, const mp_logger_stream_t *stream);

/* Destroy every registered stream and release any owned stream_context values. */
void mp_logger_destroy_streams(mp_logger_t *logger);

/* Report whether level falls within the inclusive [minimum, maximum] range. */
int mp_logger_level_in_range(mp_log_level_t level, mp_log_level_t minimum, mp_log_level_t maximum);

/* Parse one stable level name into out_level. */
mp_log_status_t mp_logger_parse_level_name(const char *value, mp_log_level_t *out_level);

/* Parse one stable format name into out_format. */
mp_log_status_t mp_logger_parse_format_name(const char *value, mp_log_format_t *out_format);

/* Trim leading and trailing ASCII whitespace while copying src into dest. */
void mp_logger_copy_trimmed(char *dest, size_t dest_capacity, const char *src);

/* Copy src into dest, always NUL-terminate, and optionally report the copied byte length. */
void mp_logger_copy_truncated(
    char *dest,
    size_t dest_capacity,
    const char *src,
    size_t *out_length);

/* Split a comma-separated active_streams list into ordered sink names. */
int mp_logger_split_stream_list(
    const char *active_streams,
    char names[][32],
    size_t max_names,
    size_t *out_count);

/* Replace unsupported path characters with safe ASCII separators. */
void mp_logger_sanitize_file_component(char *value);

/* Create path and any missing parents, returning zero on success. */
int mp_logger_ensure_directory(const char *path);

#endif
