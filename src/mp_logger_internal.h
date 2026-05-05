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

typedef struct {
    uint64_t sequence_id;
    int64_t unix_epoch_millis;
    mp_log_level_t level;
    size_t message_length;
    size_t context_length;
    char *message_buffer;
    char *context_buffer;
} mp_log_slot_t;

typedef struct {
    int backup_fd;
    char backup_path[MP_LOGGER_PATH_CAPACITY];
    pthread_mutex_t mutex;
} mp_backup_logger_t;

typedef struct {
    FILE *file_handle;
    char path[MP_LOGGER_PATH_CAPACITY];
    int close_on_destroy;
} mp_file_stream_state_t;

typedef struct {
    int socket_fd;
    struct sockaddr_storage address;
    socklen_t address_length;
    char endpoint[MP_LOGGER_HOST_CAPACITY + 16u];
} mp_udp_stream_state_t;

struct mp_logger {
    mp_logger_config_t config;
    char run_suffix[32];
    mp_log_slot_t *slots;
    char *message_storage;
    char *context_storage;
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
};

int64_t mp_logger_now_millis(void);
void mp_logger_format_timestamp(int64_t unix_epoch_millis, char *buffer, size_t buffer_capacity);
size_t mp_logger_render_record(
    const mp_logger_t *logger,
    const mp_log_record_t *record,
    char *buffer,
    size_t buffer_capacity);
mp_log_status_t mp_logger_backup_open(mp_logger_t *logger);
void mp_logger_backup_close(mp_logger_t *logger);
void mp_logger_backup_write(mp_logger_t *logger, const char *severity, const char *message);
mp_log_status_t mp_logger_build_builtin_streams(mp_logger_t *logger);
mp_log_status_t mp_logger_add_owned_stream(mp_logger_t *logger, const mp_logger_stream_t *stream);
void mp_logger_destroy_streams(mp_logger_t *logger);
int mp_logger_level_in_range(mp_log_level_t level, mp_log_level_t minimum, mp_log_level_t maximum);
mp_log_status_t mp_logger_parse_level_name(const char *value, mp_log_level_t *out_level);
mp_log_status_t mp_logger_parse_format_name(const char *value, mp_log_format_t *out_format);
void mp_logger_copy_trimmed(char *dest, size_t dest_capacity, const char *src);
void mp_logger_copy_truncated(
    char *dest,
    size_t dest_capacity,
    const char *src,
    size_t *out_length);
int mp_logger_split_stream_list(
    const char *active_streams,
    char names[][32],
    size_t max_names,
    size_t *out_count);
void mp_logger_sanitize_file_component(char *value);
int mp_logger_ensure_directory(const char *path);

#endif
