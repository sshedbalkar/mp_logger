#define _POSIX_C_SOURCE 200809L

#include "mp_logger.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    char formatted_entry[2048];
    uint64_t write_count;
} capture_stream_t;

typedef struct {
    char formatted_entry[2048];
    uint64_t write_count;
    size_t field_count;
    char tenant[64];
    int ok_value;
    int64_t attempt_value;
    uint64_t bytes_value;
    double latency_value;
} structured_capture_stream_t;

typedef struct {
    uint32_t delay_millis;
    uint64_t write_count;
} delayed_stream_t;

static mp_log_status_t capture_stream_write(
    void *stream_context,
    const mp_log_record_t *record,
    const char *formatted_entry,
    size_t formatted_entry_length) {
    capture_stream_t *capture = (capture_stream_t *)stream_context;
    size_t copy_length = formatted_entry_length;
    (void)record;
    if (capture == NULL || formatted_entry == NULL) {
        return MP_LOG_STATUS_INVALID_ARGUMENT;
    }
    if (copy_length >= sizeof(capture->formatted_entry)) {
        copy_length = sizeof(capture->formatted_entry) - 1u;
    }
    memcpy(capture->formatted_entry, formatted_entry, copy_length);
    capture->formatted_entry[copy_length] = '\0';
    capture->write_count++;
    return MP_LOG_STATUS_OK;
}

static void capture_stream_destroy(void *stream_context) {
    free(stream_context);
}

static mp_log_status_t structured_capture_stream_write(
    void *stream_context,
    const mp_log_record_t *record,
    const char *formatted_entry,
    size_t formatted_entry_length) {
    structured_capture_stream_t *capture = (structured_capture_stream_t *)stream_context;
    size_t copy_length = formatted_entry_length;
    size_t index = 0;
    if (capture == NULL || record == NULL || formatted_entry == NULL) {
        return MP_LOG_STATUS_INVALID_ARGUMENT;
    }
    if (copy_length >= sizeof(capture->formatted_entry)) {
        copy_length = sizeof(capture->formatted_entry) - 1u;
    }
    memcpy(capture->formatted_entry, formatted_entry, copy_length);
    capture->formatted_entry[copy_length] = '\0';
    capture->write_count++;
    capture->field_count = record->field_count;

    for (index = 0; index < record->field_count; index++) {
        const mp_log_field_t *field = &record->fields[index];
        if (strcmp(field->key, "tenant") == 0 && field->type == MP_LOG_FIELD_STRING) {
            (void)snprintf(capture->tenant, sizeof(capture->tenant), "%s", field->value.string_value);
        } else if (strcmp(field->key, "ok") == 0 && field->type == MP_LOG_FIELD_BOOL) {
            capture->ok_value = field->value.bool_value ? 1 : 0;
        } else if (strcmp(field->key, "attempt") == 0 && field->type == MP_LOG_FIELD_INT64) {
            capture->attempt_value = field->value.int64_value;
        } else if (strcmp(field->key, "bytes") == 0 && field->type == MP_LOG_FIELD_UINT64) {
            capture->bytes_value = field->value.uint64_value;
        } else if (strcmp(field->key, "latency_ms") == 0 && field->type == MP_LOG_FIELD_FLOAT64) {
            capture->latency_value = field->value.float64_value;
        }
    }
    return MP_LOG_STATUS_OK;
}

static void structured_capture_stream_destroy(void *stream_context) {
    free(stream_context);
}

static mp_log_status_t delayed_stream_write(
    void *stream_context,
    const mp_log_record_t *record,
    const char *formatted_entry,
    size_t formatted_entry_length) {
    delayed_stream_t *stream = (delayed_stream_t *)stream_context;
    struct timespec wait_time;
    (void)record;
    (void)formatted_entry;
    (void)formatted_entry_length;
    if (stream == NULL) {
        return MP_LOG_STATUS_INVALID_ARGUMENT;
    }
    wait_time.tv_sec = (time_t)(stream->delay_millis / 1000u);
    wait_time.tv_nsec = (long)((stream->delay_millis % 1000u) * 1000000u);
    (void)nanosleep(&wait_time, NULL);
    stream->write_count++;
    return MP_LOG_STATUS_OK;
}

static void delayed_stream_destroy(void *stream_context) {
    free(stream_context);
}

static void ensure_directory(const char *path) {
    int status = mkdir(path, 0755);
    assert(status == 0 || errno == EEXIST);
}

static void write_text_file(const char *path, const char *contents) {
    FILE *file = fopen(path, "w");
    assert(file != NULL);
    assert(fputs(contents, file) >= 0);
    assert(fclose(file) == 0);
}

static void read_text_file(const char *path, char *buffer, size_t buffer_capacity) {
    FILE *file = fopen(path, "r");
    size_t read_count = 0;
    assert(file != NULL);
    read_count = fread(buffer, 1u, buffer_capacity - 1u, file);
    buffer[read_count] = '\0';
    assert(fclose(file) == 0);
}

/* Scan a test output directory for the one run-specific log file that matches the requested prefix. */
static int find_file_with_prefix(
    const char *directory,
    const char *prefix,
    char *out_path,
    size_t out_path_capacity) {
    DIR *dir = opendir(directory);
    struct dirent *entry = NULL;
    if (dir == NULL) {
        return 0;
    }
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, prefix, strlen(prefix)) == 0 &&
            strstr(entry->d_name, ".log") != NULL) {
            (void)snprintf(out_path, out_path_capacity, "%s/%s", directory, entry->d_name);
            (void)closedir(dir);
            return 1;
        }
    }
    (void)closedir(dir);
    return 0;
}

/* Clean prior run artifacts so file-based assertions only see output produced by this test case. */
static void remove_files_with_prefix(const char *directory, const char *prefix) {
    DIR *dir = opendir(directory);
    struct dirent *entry = NULL;
    if (dir == NULL) {
        return;
    }
    while ((entry = readdir(dir)) != NULL) {
        char path[512];
        if (strncmp(entry->d_name, prefix, strlen(prefix)) != 0 ||
            strstr(entry->d_name, ".log") == NULL) {
            continue;
        }
        (void)snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
        (void)unlink(path);
    }
    (void)closedir(dir);
}

/* Fill a pre-start queue to prove saturation is reported even before the worker thread runs. */
static void test_queue_full_before_start(void) {
    mp_logger_config_t config;
    mp_logger_t *logger = NULL;
    mp_logger_stats_t stats;
    mp_logger_config_init_defaults(&config);
    config.buffer_capacity = 1u;
    config.message_capacity = 64u;
    config.context_capacity = 64u;
    (void)snprintf(config.active_streams, sizeof(config.active_streams), "%s", "");
    assert(mp_logger_create(&config, &logger) == MP_LOG_STATUS_OK);
    assert(mp_logger_log(logger, MP_LOG_LEVEL_INFO, "first", NULL) == MP_LOG_STATUS_OK);
    assert(mp_logger_log(logger, MP_LOG_LEVEL_INFO, "second", NULL) == MP_LOG_STATUS_QUEUE_FULL);
    assert(mp_logger_get_stats(logger, &stats) == MP_LOG_STATUS_OK);
    assert(stats.queued_records == 1u);
    assert(stats.dropped_full == 1u);
    mp_logger_destroy(logger);
}

/* Register a custom sink and assert it receives the fully rendered message plus context text. */
static void test_custom_stream_receives_formatted_message(void) {
    mp_logger_config_t config;
    mp_logger_t *logger = NULL;
    mp_logger_stream_t stream;
    capture_stream_t *capture = NULL;
    mp_logger_stats_t stats;

    mp_logger_config_init_defaults(&config);
    config.buffer_capacity = 4u;
    config.message_capacity = 128u;
    config.context_capacity = 128u;
    (void)snprintf(config.active_streams, sizeof(config.active_streams), "%s", "");

    assert(mp_logger_create(&config, &logger) == MP_LOG_STATUS_OK);
    capture = (capture_stream_t *)calloc(1u, sizeof(*capture));
    assert(capture != NULL);
    memset(&stream, 0, sizeof(stream));
    (void)snprintf(stream.stream_name, sizeof(stream.stream_name), "%s", "capture");
    stream.minimum_level = MP_LOG_LEVEL_TRACE;
    stream.maximum_level = MP_LOG_LEVEL_FATAL;
    stream.stream_context = capture;
    stream.write = capture_stream_write;
    stream.destroy = capture_stream_destroy;
    assert(mp_logger_add_stream(logger, &stream) == MP_LOG_STATUS_OK);
    assert(mp_logger_start(logger) == MP_LOG_STATUS_OK);
    assert(mp_logger_log(logger, MP_LOG_LEVEL_WARNING, "hello", "ctx") == MP_LOG_STATUS_OK);
    assert(mp_logger_flush(logger, 2000u) == MP_LOG_STATUS_OK);
    assert(mp_logger_shutdown(logger, 2000u) == MP_LOG_STATUS_OK);
    assert(mp_logger_get_stats(logger, &stats) == MP_LOG_STATUS_OK);
    assert(stats.processed_records == 1u);
    assert(capture->write_count == 1u);
    assert(strstr(capture->formatted_entry, "hello") != NULL);
    assert(strstr(capture->formatted_entry, "ctx") != NULL);
    mp_logger_destroy(logger);
}

/* Preserve typed structured fields across queueing and render them as first-class output fields. */
static void test_structured_fields_reach_callbacks_and_render_json(void) {
    mp_logger_config_t config;
    mp_logger_t *logger = NULL;
    mp_logger_stream_t stream;
    structured_capture_stream_t *capture = NULL;
    mp_log_field_t fields[5];

    mp_logger_config_init_defaults(&config);
    config.buffer_capacity = 4u;
    config.message_capacity = 128u;
    config.context_capacity = 128u;
    config.field_capacity = 5u;
    config.field_key_capacity = 32u;
    config.field_value_capacity = 64u;
    (void)snprintf(config.active_streams, sizeof(config.active_streams), "%s", "");

    assert(mp_logger_create(&config, &logger) == MP_LOG_STATUS_OK);
    capture = (structured_capture_stream_t *)calloc(1u, sizeof(*capture));
    assert(capture != NULL);
    memset(&stream, 0, sizeof(stream));
    (void)snprintf(stream.stream_name, sizeof(stream.stream_name), "%s", "structured");
    stream.minimum_level = MP_LOG_LEVEL_TRACE;
    stream.maximum_level = MP_LOG_LEVEL_FATAL;
    stream.stream_context = capture;
    stream.write = structured_capture_stream_write;
    stream.destroy = structured_capture_stream_destroy;
    assert(mp_logger_add_stream(logger, &stream) == MP_LOG_STATUS_OK);
    assert(mp_logger_start(logger) == MP_LOG_STATUS_OK);

    fields[0] = mp_log_field_string("tenant", "alpha");
    fields[1] = mp_log_field_bool("ok", true);
    fields[2] = mp_log_field_int64("attempt", 2);
    fields[3] = mp_log_field_uint64("bytes", 42u);
    fields[4] = mp_log_field_float64("latency_ms", 12.5);
    assert(mp_logger_log_fields(
        logger,
        MP_LOG_LEVEL_INFO,
        "structured",
        "ctx",
        fields,
        sizeof(fields) / sizeof(fields[0])) == MP_LOG_STATUS_OK);
    assert(mp_logger_flush(logger, 2000u) == MP_LOG_STATUS_OK);
    assert(mp_logger_shutdown(logger, 2000u) == MP_LOG_STATUS_OK);

    assert(capture->write_count == 1u);
    assert(capture->field_count == 5u);
    assert(strcmp(capture->tenant, "alpha") == 0);
    assert(capture->ok_value == 1);
    assert(capture->attempt_value == 2);
    assert(capture->bytes_value == 42u);
    assert(capture->latency_value == 12.5);
    assert(strstr(capture->formatted_entry, "\"tenant\":\"alpha\"") != NULL);
    assert(strstr(capture->formatted_entry, "\"ok\":true") != NULL);
    assert(strstr(capture->formatted_entry, "\"attempt\":2") != NULL);
    assert(strstr(capture->formatted_entry, "\"bytes\":42") != NULL);
    assert(strstr(capture->formatted_entry, "\"latency_ms\":12.5") != NULL);
    mp_logger_destroy(logger);
}

/* Copy structured strings into queue-owned storage so caller buffers can change immediately. */
static void test_structured_fields_copy_strings_before_flush(void) {
    mp_logger_config_t config;
    mp_logger_t *logger = NULL;
    mp_logger_stream_t stream;
    structured_capture_stream_t *capture = NULL;
    mp_log_field_t fields[1];
    char tenant_value[32];

    mp_logger_config_init_defaults(&config);
    config.buffer_capacity = 4u;
    config.message_capacity = 128u;
    config.context_capacity = 128u;
    config.field_capacity = 2u;
    config.field_key_capacity = 32u;
    config.field_value_capacity = 32u;
    (void)snprintf(config.active_streams, sizeof(config.active_streams), "%s", "");

    assert(mp_logger_create(&config, &logger) == MP_LOG_STATUS_OK);
    capture = (structured_capture_stream_t *)calloc(1u, sizeof(*capture));
    assert(capture != NULL);
    memset(&stream, 0, sizeof(stream));
    (void)snprintf(stream.stream_name, sizeof(stream.stream_name), "%s", "copy-check");
    stream.minimum_level = MP_LOG_LEVEL_TRACE;
    stream.maximum_level = MP_LOG_LEVEL_FATAL;
    stream.stream_context = capture;
    stream.write = structured_capture_stream_write;
    stream.destroy = structured_capture_stream_destroy;
    assert(mp_logger_add_stream(logger, &stream) == MP_LOG_STATUS_OK);
    assert(mp_logger_start(logger) == MP_LOG_STATUS_OK);

    (void)snprintf(tenant_value, sizeof(tenant_value), "%s", "alpha");
    fields[0] = mp_log_field_string("tenant", tenant_value);
    assert(mp_logger_log_fields(
        logger,
        MP_LOG_LEVEL_INFO,
        "copied",
        NULL,
        fields,
        sizeof(fields) / sizeof(fields[0])) == MP_LOG_STATUS_OK);
    memset(tenant_value, 'z', sizeof(tenant_value));
    tenant_value[sizeof(tenant_value) - 1u] = '\0';

    assert(mp_logger_flush(logger, 2000u) == MP_LOG_STATUS_OK);
    assert(mp_logger_shutdown(logger, 2000u) == MP_LOG_STATUS_OK);
    assert(strcmp(capture->tenant, "alpha") == 0);
    assert(strstr(capture->formatted_entry, "\"tenant\":\"alpha\"") != NULL);
    mp_logger_destroy(logger);
}

/* Reject invalid structured field sets and append valid fields to text output in order. */
static void test_structured_fields_validate_and_render_text(void) {
    mp_logger_config_t config;
    mp_logger_t *logger = NULL;
    mp_logger_stream_t stream;
    capture_stream_t *capture = NULL;
    mp_log_field_t valid_fields[2];
    mp_log_field_t reserved_field[1];
    mp_log_field_t overflow_fields[3];

    mp_logger_config_init_defaults(&config);
    config.buffer_capacity = 4u;
    config.message_capacity = 128u;
    config.context_capacity = 128u;
    config.field_capacity = 2u;
    config.field_key_capacity = 32u;
    config.field_value_capacity = 32u;
    config.format = MP_LOG_FORMAT_TEXT;
    (void)snprintf(config.active_streams, sizeof(config.active_streams), "%s", "");

    assert(mp_logger_create(&config, &logger) == MP_LOG_STATUS_OK);
    capture = (capture_stream_t *)calloc(1u, sizeof(*capture));
    assert(capture != NULL);
    memset(&stream, 0, sizeof(stream));
    (void)snprintf(stream.stream_name, sizeof(stream.stream_name), "%s", "text-capture");
    stream.minimum_level = MP_LOG_LEVEL_TRACE;
    stream.maximum_level = MP_LOG_LEVEL_FATAL;
    stream.stream_context = capture;
    stream.write = capture_stream_write;
    stream.destroy = capture_stream_destroy;
    assert(mp_logger_add_stream(logger, &stream) == MP_LOG_STATUS_OK);
    assert(mp_logger_start(logger) == MP_LOG_STATUS_OK);

    valid_fields[0] = mp_log_field_string("tenant", "alpha");
    valid_fields[1] = mp_log_field_bool("ok", true);
    assert(mp_logger_log_fields(
        logger,
        MP_LOG_LEVEL_INFO,
        "text fields",
        NULL,
        valid_fields,
        sizeof(valid_fields) / sizeof(valid_fields[0])) == MP_LOG_STATUS_OK);
    assert(mp_logger_flush(logger, 2000u) == MP_LOG_STATUS_OK);
    assert(strstr(capture->formatted_entry, "tenant=\"alpha\"") != NULL);
    assert(strstr(capture->formatted_entry, "ok=true") != NULL);

    reserved_field[0] = mp_log_field_string("message", "shadow");
    assert(mp_logger_log_fields(
        logger,
        MP_LOG_LEVEL_INFO,
        "bad",
        NULL,
        reserved_field,
        sizeof(reserved_field) / sizeof(reserved_field[0])) == MP_LOG_STATUS_INVALID_ARGUMENT);

    overflow_fields[0] = mp_log_field_string("tenant", "alpha");
    overflow_fields[1] = mp_log_field_bool("ok", true);
    overflow_fields[2] = mp_log_field_int64("attempt", 3);
    assert(mp_logger_log_fields(
        logger,
        MP_LOG_LEVEL_INFO,
        "too many",
        NULL,
        overflow_fields,
        sizeof(overflow_fields) / sizeof(overflow_fields[0])) == MP_LOG_STATUS_LIMIT_EXCEEDED);

    assert(mp_logger_shutdown(logger, 2000u) == MP_LOG_STATUS_OK);
    mp_logger_destroy(logger);
}

/* Reject malformed structured field inputs before queue admission and accept exact limit fits. */
static void test_structured_fields_validation_edge_cases(void) {
    mp_logger_config_t config;
    mp_logger_t *logger = NULL;
    char max_key[32];
    char too_long_key[33];
    char max_value[32];
    char too_long_value[33];
    mp_log_field_t exact_fit[1];
    mp_log_field_t duplicate_fields[2];
    mp_log_field_t invalid_key_fields[1];
    mp_log_field_t null_key_fields[1];
    mp_log_field_t empty_key_fields[1];
    mp_log_field_t long_key_fields[1];
    mp_log_field_t long_value_fields[1];
    mp_log_field_t null_string_fields[1];
    mp_log_field_t nan_fields[1];
    mp_log_field_t inf_fields[1];
    mp_log_field_t invalid_type_fields[1];

    mp_logger_config_init_defaults(&config);
    config.field_capacity = 2u;
    config.field_key_capacity = sizeof(max_key);
    config.field_value_capacity = sizeof(max_value);
    assert(mp_logger_create(&config, &logger) == MP_LOG_STATUS_OK);

    memset(max_key, 'k', sizeof(max_key) - 1u);
    max_key[sizeof(max_key) - 1u] = '\0';
    memset(too_long_key, 'k', sizeof(too_long_key) - 1u);
    too_long_key[sizeof(too_long_key) - 1u] = '\0';
    memset(max_value, 'v', sizeof(max_value) - 1u);
    max_value[sizeof(max_value) - 1u] = '\0';
    memset(too_long_value, 'v', sizeof(too_long_value) - 1u);
    too_long_value[sizeof(too_long_value) - 1u] = '\0';

    exact_fit[0] = mp_log_field_string(max_key, max_value);
    assert(mp_logger_log_fields(
        logger,
        MP_LOG_LEVEL_INFO,
        "exact fit",
        NULL,
        exact_fit,
        sizeof(exact_fit) / sizeof(exact_fit[0])) == MP_LOG_STATUS_OK);

    duplicate_fields[0] = mp_log_field_string("tenant", "alpha");
    duplicate_fields[1] = mp_log_field_bool("tenant", true);
    assert(mp_logger_log_fields(
        logger,
        MP_LOG_LEVEL_INFO,
        "duplicate",
        NULL,
        duplicate_fields,
        sizeof(duplicate_fields) / sizeof(duplicate_fields[0])) == MP_LOG_STATUS_INVALID_ARGUMENT);

    invalid_key_fields[0] = mp_log_field_string("tenant bad", "alpha");
    assert(mp_logger_log_fields(
        logger,
        MP_LOG_LEVEL_INFO,
        "invalid key",
        NULL,
        invalid_key_fields,
        sizeof(invalid_key_fields) / sizeof(invalid_key_fields[0])) == MP_LOG_STATUS_INVALID_ARGUMENT);

    null_key_fields[0] = mp_log_field_string(NULL, "alpha");
    assert(mp_logger_log_fields(
        logger,
        MP_LOG_LEVEL_INFO,
        "null key",
        NULL,
        null_key_fields,
        sizeof(null_key_fields) / sizeof(null_key_fields[0])) == MP_LOG_STATUS_INVALID_ARGUMENT);

    empty_key_fields[0] = mp_log_field_string("", "alpha");
    assert(mp_logger_log_fields(
        logger,
        MP_LOG_LEVEL_INFO,
        "empty key",
        NULL,
        empty_key_fields,
        sizeof(empty_key_fields) / sizeof(empty_key_fields[0])) == MP_LOG_STATUS_INVALID_ARGUMENT);

    assert(mp_logger_log_fields(
        logger,
        MP_LOG_LEVEL_INFO,
        "null fields pointer",
        NULL,
        NULL,
        1u) == MP_LOG_STATUS_INVALID_ARGUMENT);

    long_key_fields[0] = mp_log_field_string(too_long_key, "alpha");
    assert(mp_logger_log_fields(
        logger,
        MP_LOG_LEVEL_INFO,
        "long key",
        NULL,
        long_key_fields,
        sizeof(long_key_fields) / sizeof(long_key_fields[0])) == MP_LOG_STATUS_INVALID_ARGUMENT);

    long_value_fields[0] = mp_log_field_string("tenant", too_long_value);
    assert(mp_logger_log_fields(
        logger,
        MP_LOG_LEVEL_INFO,
        "long value",
        NULL,
        long_value_fields,
        sizeof(long_value_fields) / sizeof(long_value_fields[0])) == MP_LOG_STATUS_LIMIT_EXCEEDED);

    null_string_fields[0] = mp_log_field_string("tenant", NULL);
    assert(mp_logger_log_fields(
        logger,
        MP_LOG_LEVEL_INFO,
        "null string value",
        NULL,
        null_string_fields,
        sizeof(null_string_fields) / sizeof(null_string_fields[0])) == MP_LOG_STATUS_OK);

    nan_fields[0] = mp_log_field_float64("latency_ms", NAN);
    assert(mp_logger_log_fields(
        logger,
        MP_LOG_LEVEL_INFO,
        "nan",
        NULL,
        nan_fields,
        sizeof(nan_fields) / sizeof(nan_fields[0])) == MP_LOG_STATUS_INVALID_ARGUMENT);

    inf_fields[0] = mp_log_field_float64("latency_ms", INFINITY);
    assert(mp_logger_log_fields(
        logger,
        MP_LOG_LEVEL_INFO,
        "inf",
        NULL,
        inf_fields,
        sizeof(inf_fields) / sizeof(inf_fields[0])) == MP_LOG_STATUS_INVALID_ARGUMENT);

    invalid_type_fields[0] = mp_log_field_string("tenant", "alpha");
    invalid_type_fields[0].type = (mp_log_field_type_t)99;
    assert(mp_logger_log_fields(
        logger,
        MP_LOG_LEVEL_INFO,
        "bad type",
        NULL,
        invalid_type_fields,
        sizeof(invalid_type_fields) / sizeof(invalid_type_fields[0])) == MP_LOG_STATUS_INVALID_ARGUMENT);

    mp_logger_destroy(logger);
}

/* Escape structured string values so they cannot forge JSON objects or text log lines. */
static void test_structured_fields_escape_render_output(void) {
    mp_logger_config_t config;
    mp_logger_t *json_logger = NULL;
    mp_logger_t *text_logger = NULL;
    mp_logger_stream_t json_stream;
    mp_logger_stream_t text_stream;
    capture_stream_t *json_capture = NULL;
    capture_stream_t *text_capture = NULL;
    mp_log_field_t fields[1];

    mp_logger_config_init_defaults(&config);
    config.buffer_capacity = 4u;
    config.message_capacity = 128u;
    config.context_capacity = 128u;
    config.field_capacity = 2u;
    config.field_key_capacity = 32u;
    config.field_value_capacity = 64u;
    (void)snprintf(config.active_streams, sizeof(config.active_streams), "%s", "");

    assert(mp_logger_create(&config, &json_logger) == MP_LOG_STATUS_OK);
    json_capture = (capture_stream_t *)calloc(1u, sizeof(*json_capture));
    assert(json_capture != NULL);
    memset(&json_stream, 0, sizeof(json_stream));
    (void)snprintf(json_stream.stream_name, sizeof(json_stream.stream_name), "%s", "json-escape");
    json_stream.minimum_level = MP_LOG_LEVEL_TRACE;
    json_stream.maximum_level = MP_LOG_LEVEL_FATAL;
    json_stream.stream_context = json_capture;
    json_stream.write = capture_stream_write;
    json_stream.destroy = capture_stream_destroy;
    assert(mp_logger_add_stream(json_logger, &json_stream) == MP_LOG_STATUS_OK);
    assert(mp_logger_start(json_logger) == MP_LOG_STATUS_OK);

    fields[0] = mp_log_field_string("tenant", "alpha\"\nrole=admin\t\\");
    assert(mp_logger_log_fields(
        json_logger,
        MP_LOG_LEVEL_INFO,
        "escaped",
        NULL,
        fields,
        sizeof(fields) / sizeof(fields[0])) == MP_LOG_STATUS_OK);
    assert(mp_logger_flush(json_logger, 2000u) == MP_LOG_STATUS_OK);
    assert(mp_logger_shutdown(json_logger, 2000u) == MP_LOG_STATUS_OK);
    assert(strstr(json_capture->formatted_entry, "\"tenant\":\"alpha\\\"\\nrole=admin\\t\\\\\"") != NULL);
    mp_logger_destroy(json_logger);

    mp_logger_config_init_defaults(&config);
    config.buffer_capacity = 4u;
    config.message_capacity = 128u;
    config.context_capacity = 128u;
    config.field_capacity = 2u;
    config.field_key_capacity = 32u;
    config.field_value_capacity = 64u;
    config.format = MP_LOG_FORMAT_TEXT;
    (void)snprintf(config.active_streams, sizeof(config.active_streams), "%s", "");

    assert(mp_logger_create(&config, &text_logger) == MP_LOG_STATUS_OK);
    text_capture = (capture_stream_t *)calloc(1u, sizeof(*text_capture));
    assert(text_capture != NULL);
    memset(&text_stream, 0, sizeof(text_stream));
    (void)snprintf(text_stream.stream_name, sizeof(text_stream.stream_name), "%s", "text-escape");
    text_stream.minimum_level = MP_LOG_LEVEL_TRACE;
    text_stream.maximum_level = MP_LOG_LEVEL_FATAL;
    text_stream.stream_context = text_capture;
    text_stream.write = capture_stream_write;
    text_stream.destroy = capture_stream_destroy;
    assert(mp_logger_add_stream(text_logger, &text_stream) == MP_LOG_STATUS_OK);
    assert(mp_logger_start(text_logger) == MP_LOG_STATUS_OK);

    fields[0] = mp_log_field_string("tenant", "alpha\"\nrole=admin\t\\");
    assert(mp_logger_log_fields(
        text_logger,
        MP_LOG_LEVEL_INFO,
        "escaped",
        NULL,
        fields,
        sizeof(fields) / sizeof(fields[0])) == MP_LOG_STATUS_OK);
    assert(mp_logger_flush(text_logger, 2000u) == MP_LOG_STATUS_OK);
    assert(mp_logger_shutdown(text_logger, 2000u) == MP_LOG_STATUS_OK);
    assert(strstr(text_capture->formatted_entry, "tenant=\"alpha\\\"\\nrole=admin\\t\\\\\"") != NULL);
    mp_logger_destroy(text_logger);
}

/* Report whether any currently registered stream would accept a given level. */
static void test_level_enabled_reports_stream_matches(void) {
    mp_logger_config_t config;
    mp_logger_t *logger = NULL;
    mp_logger_stream_t stream;
    capture_stream_t *capture = NULL;

    mp_logger_config_init_defaults(&config);
    (void)snprintf(config.active_streams, sizeof(config.active_streams), "%s", "");

    assert(!mp_logger_is_level_enabled(NULL, MP_LOG_LEVEL_INFO));
    assert(mp_logger_create(&config, &logger) == MP_LOG_STATUS_OK);
    assert(!mp_logger_is_level_enabled(logger, MP_LOG_LEVEL_INFO));

    capture = (capture_stream_t *)calloc(1u, sizeof(*capture));
    assert(capture != NULL);
    memset(&stream, 0, sizeof(stream));
    (void)snprintf(stream.stream_name, sizeof(stream.stream_name), "%s", "warnings");
    stream.minimum_level = MP_LOG_LEVEL_WARNING;
    stream.maximum_level = MP_LOG_LEVEL_ERROR;
    stream.stream_context = capture;
    stream.write = capture_stream_write;
    stream.destroy = capture_stream_destroy;
    assert(mp_logger_add_stream(logger, &stream) == MP_LOG_STATUS_OK);

    assert(!mp_logger_is_level_enabled(logger, MP_LOG_LEVEL_INFO));
    assert(mp_logger_is_level_enabled(logger, MP_LOG_LEVEL_WARNING));
    assert(mp_logger_is_level_enabled(logger, MP_LOG_LEVEL_ERROR));
    assert(!mp_logger_is_level_enabled(logger, MP_LOG_LEVEL_FATAL));

    assert(mp_logger_start(logger) == MP_LOG_STATUS_OK);
    assert(mp_logger_is_level_enabled(logger, MP_LOG_LEVEL_WARNING));
    assert(mp_logger_shutdown(logger, 2000u) == MP_LOG_STATUS_OK);
    assert(!mp_logger_is_level_enabled(logger, MP_LOG_LEVEL_WARNING));
    mp_logger_destroy(logger);
}

/* Parse an INI override file and verify the loader updates every touched field. */
static void test_bootstrap_load_applies_overrides(void) {
    const char *config_path = ".tmp/bootstrap-test.ini";
    mp_logger_config_t config;
    ensure_directory(".tmp");
    write_text_file(
        config_path,
        "service_name = unit-service\n"
        "environment_name = staging\n"
        "\n"
        "[logger]\n"
        "buffer_capacity = 8\n"
        "message_capacity = 96\n"
        "context_capacity = 112\n"
        "field_capacity = 6\n"
        "field_key_capacity = 40\n"
        "field_value_capacity = 80\n"
        "format = text\n"
        "pretty_output = true\n"
        "log_directory = .tmp/logger-bootstrap\n"
        "file_name_prefix = bootstrap-log\n"
        "backup_file_name_prefix = bootstrap-internal\n"
        "active_streams = file,udp\n"
        "\n"
        "[file]\n"
        "minimum_level = debug\n"
        "maximum_level = fatal\n"
        "\n"
        "[udp]\n"
        "minimum_level = error\n"
        "maximum_level = fatal\n"
        "host = 127.0.0.1\n"
        "port = 6500\n");
    assert(mp_logger_bootstrap_load(config_path, &config) == MP_LOG_STATUS_OK);
    assert(strcmp(config.service_name, "unit-service") == 0);
    assert(strcmp(config.environment_name, "staging") == 0);
    assert(config.buffer_capacity == 8u);
    assert(config.message_capacity == 96u);
    assert(config.context_capacity == 112u);
    assert(config.field_capacity == 6u);
    assert(config.field_key_capacity == 40u);
    assert(config.field_value_capacity == 80u);
    assert(config.format == MP_LOG_FORMAT_TEXT);
    assert(config.pretty_output == 1);
    assert(strcmp(config.file_name_prefix, "bootstrap-log") == 0);
    assert(strcmp(config.backup_file_name_prefix, "bootstrap-internal") == 0);
    assert(strcmp(config.active_streams, "file,udp") == 0);
    assert(config.file_min_level == MP_LOG_LEVEL_DEBUG);
    assert(config.udp_port == 6500u);
}

/* Use the builtin file sink and then locate the per-run log file written by that logger instance. */
static void test_file_stream_writes_new_run_file(void) {
    mp_logger_config_t config;
    mp_logger_t *logger = NULL;
    char log_path[256];
    char log_contents[4096];
    ensure_directory(".tmp");
    ensure_directory(".tmp/logger-output");
    remove_files_with_prefix(".tmp/logger-output", "unit-file");

    mp_logger_config_init_defaults(&config);
    config.buffer_capacity = 4u;
    config.message_capacity = 128u;
    config.context_capacity = 128u;
    (void)snprintf(config.active_streams, sizeof(config.active_streams), "%s", "file");
    (void)snprintf(config.log_directory, sizeof(config.log_directory), "%s", ".tmp/logger-output");
    (void)snprintf(config.file_name_prefix, sizeof(config.file_name_prefix), "%s", "unit-file");
    (void)snprintf(
        config.backup_file_name_prefix,
        sizeof(config.backup_file_name_prefix),
        "%s",
        "unit-backup");

    assert(mp_logger_create(&config, &logger) == MP_LOG_STATUS_OK);
    assert(mp_logger_start(logger) == MP_LOG_STATUS_OK);
    assert(mp_logger_log(logger, MP_LOG_LEVEL_ERROR, "disk check", "backup path") == MP_LOG_STATUS_OK);
    assert(mp_logger_flush(logger, 2000u) == MP_LOG_STATUS_OK);
    assert(mp_logger_shutdown(logger, 2000u) == MP_LOG_STATUS_OK);
    mp_logger_destroy(logger);

    assert(find_file_with_prefix(".tmp/logger-output", "unit-file", log_path, sizeof(log_path)));
    read_text_file(log_path, log_contents, sizeof(log_contents));
    assert(strstr(log_contents, "disk check") != NULL);
    assert(strstr(log_contents, "backup path") != NULL);
}

/* Force queue saturation behind a slow custom stream and confirm the backup logger records the warning. */
static void test_buffer_saturation_writes_backup_warning(void) {
    const char *log_directory = ".tmp/logger-overflow";
    mp_logger_config_t config;
    mp_logger_t *logger = NULL;
    mp_logger_stream_t stream;
    delayed_stream_t *delayed_stream = NULL;
    mp_logger_stats_t stats;
    char backup_path[256];
    char backup_contents[4096];
    size_t index = 0;
    uint64_t full_failures = 0u;

    ensure_directory(".tmp");
    ensure_directory(log_directory);
    remove_files_with_prefix(log_directory, "overflow-backup");

    mp_logger_config_init_defaults(&config);
    config.buffer_capacity = 2u;
    config.message_capacity = 64u;
    config.context_capacity = 64u;
    (void)snprintf(config.active_streams, sizeof(config.active_streams), "%s", "");
    (void)snprintf(config.log_directory, sizeof(config.log_directory), "%s", log_directory);
    (void)snprintf(config.file_name_prefix, sizeof(config.file_name_prefix), "%s", "overflow-primary");
    (void)snprintf(
        config.backup_file_name_prefix,
        sizeof(config.backup_file_name_prefix),
        "%s",
        "overflow-backup");

    assert(mp_logger_create(&config, &logger) == MP_LOG_STATUS_OK);
    delayed_stream = (delayed_stream_t *)calloc(1u, sizeof(*delayed_stream));
    assert(delayed_stream != NULL);
    delayed_stream->delay_millis = 100u;
    memset(&stream, 0, sizeof(stream));
    (void)snprintf(stream.stream_name, sizeof(stream.stream_name), "%s", "delayed");
    stream.minimum_level = MP_LOG_LEVEL_TRACE;
    stream.maximum_level = MP_LOG_LEVEL_FATAL;
    stream.stream_context = delayed_stream;
    stream.write = delayed_stream_write;
    stream.destroy = delayed_stream_destroy;
    assert(mp_logger_add_stream(logger, &stream) == MP_LOG_STATUS_OK);
    assert(mp_logger_start(logger) == MP_LOG_STATUS_OK);

    for (index = 0; index < 8u; index++) {
        mp_log_status_t status = mp_logger_log(logger, MP_LOG_LEVEL_INFO, "overflow", "pressure");
        if (status == MP_LOG_STATUS_QUEUE_FULL) {
            full_failures++;
        }
    }

    assert(mp_logger_flush(logger, 5000u) == MP_LOG_STATUS_OK);
    assert(mp_logger_shutdown(logger, 5000u) == MP_LOG_STATUS_OK);
    assert(mp_logger_get_stats(logger, &stats) == MP_LOG_STATUS_OK);
    assert(full_failures > 0u);
    assert(stats.dropped_full == full_failures);
    mp_logger_destroy(logger);

    assert(find_file_with_prefix(log_directory, "overflow-backup", backup_path, sizeof(backup_path)));
    read_text_file(backup_path, backup_contents, sizeof(backup_contents));
    assert(strstr(backup_contents, "buffer saturation dropped records") != NULL);
}

/* Add streams until the hard limit is reached and verify the next registration is rejected. */
static void test_stream_limit_is_enforced(void) {
    mp_logger_config_t config;
    mp_logger_t *logger = NULL;
    size_t index = 0u;

    mp_logger_config_init_defaults(&config);
    (void)snprintf(config.active_streams, sizeof(config.active_streams), "%s", "");
    assert(mp_logger_create(&config, &logger) == MP_LOG_STATUS_OK);

    for (index = 0u; index < 8u; index++) {
        mp_logger_stream_t stream;
        capture_stream_t *capture = (capture_stream_t *)calloc(1u, sizeof(*capture));
        assert(capture != NULL);
        memset(&stream, 0, sizeof(stream));
        (void)snprintf(stream.stream_name, sizeof(stream.stream_name), "capture-%zu", index + 1u);
        stream.minimum_level = MP_LOG_LEVEL_TRACE;
        stream.maximum_level = MP_LOG_LEVEL_FATAL;
        stream.stream_context = capture;
        stream.write = capture_stream_write;
        stream.destroy = capture_stream_destroy;
        assert(mp_logger_add_stream(logger, &stream) == MP_LOG_STATUS_OK);
    }

    {
        mp_logger_stream_t overflow_stream;
        capture_stream_t *capture = (capture_stream_t *)calloc(1u, sizeof(*capture));
        assert(capture != NULL);
        memset(&overflow_stream, 0, sizeof(overflow_stream));
        (void)snprintf(overflow_stream.stream_name, sizeof(overflow_stream.stream_name), "%s", "capture-overflow");
        overflow_stream.minimum_level = MP_LOG_LEVEL_TRACE;
        overflow_stream.maximum_level = MP_LOG_LEVEL_FATAL;
        overflow_stream.stream_context = capture;
        overflow_stream.write = capture_stream_write;
        overflow_stream.destroy = capture_stream_destroy;
        assert(mp_logger_add_stream(logger, &overflow_stream) == MP_LOG_STATUS_LIMIT_EXCEEDED);
        capture_stream_destroy(capture);
    }

    mp_logger_destroy(logger);
}

int main(void) {
    test_queue_full_before_start();
    test_custom_stream_receives_formatted_message();
    test_structured_fields_reach_callbacks_and_render_json();
    test_structured_fields_copy_strings_before_flush();
    test_structured_fields_validate_and_render_text();
    test_structured_fields_validation_edge_cases();
    test_structured_fields_escape_render_output();
    test_level_enabled_reports_stream_matches();
    test_bootstrap_load_applies_overrides();
    test_file_stream_writes_new_run_file();
    test_buffer_saturation_writes_backup_warning();
    test_stream_limit_is_enforced();
    return 0;
}
