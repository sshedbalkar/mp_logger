#define _POSIX_C_SOURCE 200809L

#include "mp_logger.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    char formatted_entry[2048];
    uint64_t write_count;
} capture_stream_t;

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
    assert(config.format == MP_LOG_FORMAT_TEXT);
    assert(config.pretty_output == 1);
    assert(strcmp(config.file_name_prefix, "bootstrap-log") == 0);
    assert(strcmp(config.backup_file_name_prefix, "bootstrap-internal") == 0);
    assert(strcmp(config.active_streams, "file,udp") == 0);
    assert(config.file_min_level == MP_LOG_LEVEL_DEBUG);
    assert(config.udp_port == 6500u);
}

static void test_file_stream_writes_new_run_file(void) {
    mp_logger_config_t config;
    mp_logger_t *logger = NULL;
    char log_path[256];
    char log_contents[4096];
    ensure_directory(".tmp");
    ensure_directory(".tmp/logger-output");

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

int main(void) {
    test_queue_full_before_start();
    test_custom_stream_receives_formatted_message();
    test_bootstrap_load_applies_overrides();
    test_file_stream_writes_new_run_file();
    return 0;
}
