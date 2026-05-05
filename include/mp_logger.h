#ifndef MP_LOGGER_H
#define MP_LOGGER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MP_LOGGER_NAME_CAPACITY 64u
#define MP_LOGGER_PATH_CAPACITY 256u
#define MP_LOGGER_ACTIVE_STREAMS_CAPACITY 128u
#define MP_LOGGER_HOST_CAPACITY 128u

typedef enum {
    MP_LOG_LEVEL_TRACE = 0,
    MP_LOG_LEVEL_DEBUG = 1,
    MP_LOG_LEVEL_INFO = 2,
    MP_LOG_LEVEL_WARNING = 3,
    MP_LOG_LEVEL_ERROR = 4,
    MP_LOG_LEVEL_FATAL = 5
} mp_log_level_t;

typedef enum {
    MP_LOG_FORMAT_TEXT = 0,
    MP_LOG_FORMAT_JSON = 1
} mp_log_format_t;

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

typedef struct {
    uint64_t sequence_id;
    int64_t unix_epoch_millis;
    mp_log_level_t level;
    const char *message;
    const char *context_text;
} mp_log_record_t;

typedef struct {
    char service_name[MP_LOGGER_NAME_CAPACITY];
    char environment_name[MP_LOGGER_NAME_CAPACITY];
    size_t buffer_capacity;
    size_t message_capacity;
    size_t context_capacity;
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

typedef mp_log_status_t (*mp_logger_stream_write_fn)(
    void *stream_context,
    const mp_log_record_t *record,
    const char *formatted_entry,
    size_t formatted_entry_length);

typedef void (*mp_logger_stream_destroy_fn)(void *stream_context);

typedef struct {
    char stream_name[MP_LOGGER_NAME_CAPACITY];
    mp_log_level_t minimum_level;
    mp_log_level_t maximum_level;
    void *stream_context;
    mp_logger_stream_write_fn write;
    mp_logger_stream_destroy_fn destroy;
} mp_logger_stream_t;

typedef struct {
    uint64_t queued_records;
    uint64_t processed_records;
    uint64_t dropped_busy;
    uint64_t dropped_full;
    size_t active_stream_count;
} mp_logger_stats_t;

void mp_logger_config_init_defaults(mp_logger_config_t *config);

mp_log_status_t mp_logger_bootstrap_load(const char *config_path, mp_logger_config_t *out_config);
mp_log_status_t mp_logger_create(const mp_logger_config_t *config, mp_logger_t **out_logger);
mp_log_status_t mp_logger_create_from_bootstrap(const char *config_path, mp_logger_t **out_logger);
mp_log_status_t mp_logger_add_stream(mp_logger_t *logger, const mp_logger_stream_t *stream);
mp_log_status_t mp_logger_start(mp_logger_t *logger);
mp_log_status_t mp_logger_log(
    mp_logger_t *logger,
    mp_log_level_t level,
    const char *message,
    const char *context_text);
mp_log_status_t mp_logger_flush(mp_logger_t *logger, uint32_t timeout_millis);
mp_log_status_t mp_logger_get_stats(const mp_logger_t *logger, mp_logger_stats_t *out_stats);
mp_log_status_t mp_logger_shutdown(mp_logger_t *logger, uint32_t timeout_millis);
void mp_logger_destroy(mp_logger_t *logger);

const char *mp_log_level_name(mp_log_level_t level);
const char *mp_log_status_name(mp_log_status_t status);

#ifdef __cplusplus
}
#endif

#endif
