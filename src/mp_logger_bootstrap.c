#include "mp_logger_internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int mp_logger_equal_ignore_case(const char *left, const char *right) {
    size_t index = 0;
    if (left == NULL || right == NULL) {
        return 0;
    }
    while (left[index] != '\0' && right[index] != '\0') {
        if (tolower((unsigned char)left[index]) != tolower((unsigned char)right[index])) {
            return 0;
        }
        index++;
    }
    return left[index] == '\0' && right[index] == '\0';
}

static int mp_logger_parse_bool(const char *value, int *out_bool) {
    if (value == NULL || out_bool == NULL) {
        return 0;
    }
    if (mp_logger_equal_ignore_case(value, "true") || strcmp(value, "1") == 0 ||
        mp_logger_equal_ignore_case(value, "yes")) {
        *out_bool = 1;
        return 1;
    }
    if (mp_logger_equal_ignore_case(value, "false") || strcmp(value, "0") == 0 ||
        mp_logger_equal_ignore_case(value, "no")) {
        *out_bool = 0;
        return 1;
    }
    return 0;
}

static int mp_logger_parse_size_value(const char *value, size_t *out_size) {
    char *end = NULL;
    unsigned long long parsed = 0;
    if (value == NULL || out_size == NULL) {
        return 0;
    }
    errno = 0;
    parsed = strtoull(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0') {
        return 0;
    }
    *out_size = (size_t)parsed;
    return 1;
}

static int mp_logger_parse_port_value(const char *value, uint16_t *out_port) {
    size_t parsed = 0;
    if (value == NULL || out_port == NULL) {
        return 0;
    }
    if (!mp_logger_parse_size_value(value, &parsed) || parsed > UINT16_MAX) {
        return 0;
    }
    *out_port = (uint16_t)parsed;
    return 1;
}

/* Trim leading and trailing ASCII whitespace in place while preserving an empty-string fallback. */
void mp_logger_copy_trimmed(char *dest, size_t dest_capacity, const char *src) {
    size_t start = 0;
    size_t end = 0;
    size_t length = 0;
    if (dest == NULL || dest_capacity == 0) {
        return;
    }
    if (src == NULL) {
        dest[0] = '\0';
        return;
    }
    while (src[start] != '\0' && isspace((unsigned char)src[start])) {
        start++;
    }
    end = strlen(src);
    while (end > start && isspace((unsigned char)src[end - 1])) {
        end--;
    }
    length = end - start;
    if (length >= dest_capacity) {
        length = dest_capacity - 1;
    }
    if (length > 0) {
        memmove(dest, src + start, length);
    }
    dest[length] = '\0';
}

static mp_log_status_t mp_logger_apply_root_value(
    const char *key,
    const char *value,
    mp_logger_config_t *config) {
    if (strcmp(key, "service_name") == 0) {
        mp_logger_copy_trimmed(config->service_name, sizeof(config->service_name), value);
        return MP_LOG_STATUS_OK;
    }
    if (strcmp(key, "environment_name") == 0) {
        mp_logger_copy_trimmed(config->environment_name, sizeof(config->environment_name), value);
        return MP_LOG_STATUS_OK;
    }
    return MP_LOG_STATUS_CONFIG_ERROR;
}

/* Route each logger-scoped key through the right parser instead of guessing by value shape. */
static mp_log_status_t mp_logger_apply_logger_value(
    const char *key,
    const char *value,
    mp_logger_config_t *config) {
    size_t parsed_size = 0;
    int parsed_bool = 0;
    if (strcmp(key, "buffer_capacity") == 0) {
        if (!mp_logger_parse_size_value(value, &parsed_size)) {
            return MP_LOG_STATUS_CONFIG_ERROR;
        }
        config->buffer_capacity = parsed_size;
        return MP_LOG_STATUS_OK;
    }
    if (strcmp(key, "message_capacity") == 0) {
        if (!mp_logger_parse_size_value(value, &parsed_size)) {
            return MP_LOG_STATUS_CONFIG_ERROR;
        }
        config->message_capacity = parsed_size;
        return MP_LOG_STATUS_OK;
    }
    if (strcmp(key, "context_capacity") == 0) {
        if (!mp_logger_parse_size_value(value, &parsed_size)) {
            return MP_LOG_STATUS_CONFIG_ERROR;
        }
        config->context_capacity = parsed_size;
        return MP_LOG_STATUS_OK;
    }
    if (strcmp(key, "format") == 0) {
        return mp_logger_parse_format_name(value, &config->format);
    }
    if (strcmp(key, "pretty_output") == 0) {
        if (!mp_logger_parse_bool(value, &parsed_bool)) {
            return MP_LOG_STATUS_CONFIG_ERROR;
        }
        config->pretty_output = parsed_bool;
        return MP_LOG_STATUS_OK;
    }
    if (strcmp(key, "log_directory") == 0) {
        mp_logger_copy_trimmed(config->log_directory, sizeof(config->log_directory), value);
        return MP_LOG_STATUS_OK;
    }
    if (strcmp(key, "file_name_prefix") == 0) {
        mp_logger_copy_trimmed(config->file_name_prefix, sizeof(config->file_name_prefix), value);
        return MP_LOG_STATUS_OK;
    }
    if (strcmp(key, "backup_file_name_prefix") == 0) {
        mp_logger_copy_trimmed(
            config->backup_file_name_prefix,
            sizeof(config->backup_file_name_prefix),
            value);
        return MP_LOG_STATUS_OK;
    }
    if (strcmp(key, "active_streams") == 0) {
        mp_logger_copy_trimmed(config->active_streams, sizeof(config->active_streams), value);
        return MP_LOG_STATUS_OK;
    }
    return MP_LOG_STATUS_CONFIG_ERROR;
}

static mp_log_status_t mp_logger_apply_stdout_value(
    const char *key,
    const char *value,
    mp_logger_config_t *config) {
    if (strcmp(key, "minimum_level") == 0) {
        return mp_logger_parse_level_name(value, &config->stdout_min_level);
    }
    if (strcmp(key, "maximum_level") == 0) {
        return mp_logger_parse_level_name(value, &config->stdout_max_level);
    }
    return MP_LOG_STATUS_CONFIG_ERROR;
}

static mp_log_status_t mp_logger_apply_stderr_value(
    const char *key,
    const char *value,
    mp_logger_config_t *config) {
    if (strcmp(key, "minimum_level") == 0) {
        return mp_logger_parse_level_name(value, &config->stderr_min_level);
    }
    if (strcmp(key, "maximum_level") == 0) {
        return mp_logger_parse_level_name(value, &config->stderr_max_level);
    }
    return MP_LOG_STATUS_CONFIG_ERROR;
}

static mp_log_status_t mp_logger_apply_file_value(
    const char *key,
    const char *value,
    mp_logger_config_t *config) {
    if (strcmp(key, "minimum_level") == 0) {
        return mp_logger_parse_level_name(value, &config->file_min_level);
    }
    if (strcmp(key, "maximum_level") == 0) {
        return mp_logger_parse_level_name(value, &config->file_max_level);
    }
    return MP_LOG_STATUS_CONFIG_ERROR;
}

static mp_log_status_t mp_logger_apply_udp_value(
    const char *key,
    const char *value,
    mp_logger_config_t *config) {
    if (strcmp(key, "minimum_level") == 0) {
        return mp_logger_parse_level_name(value, &config->udp_min_level);
    }
    if (strcmp(key, "maximum_level") == 0) {
        return mp_logger_parse_level_name(value, &config->udp_max_level);
    }
    if (strcmp(key, "host") == 0) {
        mp_logger_copy_trimmed(config->udp_host, sizeof(config->udp_host), value);
        return MP_LOG_STATUS_OK;
    }
    if (strcmp(key, "port") == 0) {
        if (!mp_logger_parse_port_value(value, &config->udp_port)) {
            return MP_LOG_STATUS_CONFIG_ERROR;
        }
        return MP_LOG_STATUS_OK;
    }
    return MP_LOG_STATUS_CONFIG_ERROR;
}

/* Keep section dispatch explicit so unknown sections and keys fail closed. */
static mp_log_status_t mp_logger_apply_value(
    const char *section,
    const char *key,
    const char *value,
    mp_logger_config_t *config) {
    if (section[0] == '\0') {
        return mp_logger_apply_root_value(key, value, config);
    }
    if (strcmp(section, "logger") == 0) {
        return mp_logger_apply_logger_value(key, value, config);
    }
    if (strcmp(section, "stdout") == 0) {
        return mp_logger_apply_stdout_value(key, value, config);
    }
    if (strcmp(section, "stderr") == 0) {
        return mp_logger_apply_stderr_value(key, value, config);
    }
    if (strcmp(section, "file") == 0) {
        return mp_logger_apply_file_value(key, value, config);
    }
    if (strcmp(section, "udp") == 0) {
        return mp_logger_apply_udp_value(key, value, config);
    }
    return MP_LOG_STATUS_CONFIG_ERROR;
}

/* Centralize every documented default in one place so file-backed and code-backed config agree. */
void mp_logger_config_init_defaults(mp_logger_config_t *config) {
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    mp_logger_copy_trimmed(config->service_name, sizeof(config->service_name), "mp-service");
    mp_logger_copy_trimmed(
        config->environment_name,
        sizeof(config->environment_name),
        "development");
    config->buffer_capacity = 1024u;
    config->message_capacity = 512u;
    config->context_capacity = 1024u;
    config->format = MP_LOG_FORMAT_JSON;
    config->pretty_output = 0;
    mp_logger_copy_trimmed(config->log_directory, sizeof(config->log_directory), ".");
    mp_logger_copy_trimmed(config->file_name_prefix, sizeof(config->file_name_prefix), "mp-service");
    mp_logger_copy_trimmed(
        config->backup_file_name_prefix,
        sizeof(config->backup_file_name_prefix),
        "mp-logger-internal");
    mp_logger_copy_trimmed(
        config->active_streams,
        sizeof(config->active_streams),
        "stdout,stderr,file");
    config->stdout_min_level = MP_LOG_LEVEL_TRACE;
    config->stdout_max_level = MP_LOG_LEVEL_INFO;
    config->stderr_min_level = MP_LOG_LEVEL_WARNING;
    config->stderr_max_level = MP_LOG_LEVEL_FATAL;
    config->file_min_level = MP_LOG_LEVEL_TRACE;
    config->file_max_level = MP_LOG_LEVEL_FATAL;
    config->udp_min_level = MP_LOG_LEVEL_ERROR;
    config->udp_max_level = MP_LOG_LEVEL_FATAL;
    mp_logger_copy_trimmed(config->udp_host, sizeof(config->udp_host), "127.0.0.1");
    config->udp_port = 5514u;
}

/* Accept the documented level aliases while rejecting anything the logger would render ambiguously. */
mp_log_status_t mp_logger_parse_level_name(const char *value, mp_log_level_t *out_level) {
    if (value == NULL || out_level == NULL) {
        return MP_LOG_STATUS_INVALID_ARGUMENT;
    }
    if (mp_logger_equal_ignore_case(value, "trace")) {
        *out_level = MP_LOG_LEVEL_TRACE;
        return MP_LOG_STATUS_OK;
    }
    if (mp_logger_equal_ignore_case(value, "debug")) {
        *out_level = MP_LOG_LEVEL_DEBUG;
        return MP_LOG_STATUS_OK;
    }
    if (mp_logger_equal_ignore_case(value, "info")) {
        *out_level = MP_LOG_LEVEL_INFO;
        return MP_LOG_STATUS_OK;
    }
    if (mp_logger_equal_ignore_case(value, "warn") ||
        mp_logger_equal_ignore_case(value, "warning")) {
        *out_level = MP_LOG_LEVEL_WARNING;
        return MP_LOG_STATUS_OK;
    }
    if (mp_logger_equal_ignore_case(value, "error")) {
        *out_level = MP_LOG_LEVEL_ERROR;
        return MP_LOG_STATUS_OK;
    }
    if (mp_logger_equal_ignore_case(value, "fatal")) {
        *out_level = MP_LOG_LEVEL_FATAL;
        return MP_LOG_STATUS_OK;
    }
    return MP_LOG_STATUS_CONFIG_ERROR;
}

/* Restrict format parsing to the built-in text and JSON renderers. */
mp_log_status_t mp_logger_parse_format_name(const char *value, mp_log_format_t *out_format) {
    if (value == NULL || out_format == NULL) {
        return MP_LOG_STATUS_INVALID_ARGUMENT;
    }
    if (mp_logger_equal_ignore_case(value, "text")) {
        *out_format = MP_LOG_FORMAT_TEXT;
        return MP_LOG_STATUS_OK;
    }
    if (mp_logger_equal_ignore_case(value, "json")) {
        *out_format = MP_LOG_FORMAT_JSON;
        return MP_LOG_STATUS_OK;
    }
    return MP_LOG_STATUS_CONFIG_ERROR;
}

/* Split the comma-separated stream list in place so builtin stream activation stays order-preserving. */
int mp_logger_split_stream_list(
    const char *active_streams,
    char names[][32],
    size_t max_names,
    size_t *out_count) {
    char buffer[MP_LOGGER_ACTIVE_STREAMS_CAPACITY];
    char *cursor = NULL;
    size_t count = 0;
    if (names == NULL || out_count == NULL) {
        return 0;
    }
    *out_count = 0;
    if (active_streams == NULL || active_streams[0] == '\0') {
        return 1;
    }
    mp_logger_copy_trimmed(buffer, sizeof(buffer), active_streams);
    cursor = buffer;
    while (*cursor != '\0') {
        char *comma = strchr(cursor, ',');
        if (count >= max_names) {
            return 0;
        }
        if (comma != NULL) {
            *comma = '\0';
        }
        mp_logger_copy_trimmed(names[count], 32u, cursor);
        if (names[count][0] != '\0') {
            count++;
        }
        if (comma == NULL) {
            break;
        }
        cursor = comma + 1;
    }
    *out_count = count;
    return 1;
}

/*
 * Parse the bootstrap file one trimmed line at a time.
 * The loader keeps section state explicitly and rejects malformed lines immediately so config
 * errors do not turn into partial logger startup with guessed defaults.
 */
mp_log_status_t mp_logger_bootstrap_load(const char *config_path, mp_logger_config_t *out_config) {
    FILE *file = NULL;
    mp_logger_config_t config;
    char line[512];
    char current_section[32] = "";
    if (config_path == NULL || out_config == NULL) {
        return MP_LOG_STATUS_INVALID_ARGUMENT;
    }

    mp_logger_config_init_defaults(&config);
    file = fopen(config_path, "r");
    if (file == NULL) {
        return MP_LOG_STATUS_IO_ERROR;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *separator = NULL;
        char key[64];
        char value[256];
        mp_logger_copy_trimmed(line, sizeof(line), line);
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';') {
            continue;
        }
        if (line[0] == '[') {
            size_t section_length = strlen(line);
            if (section_length < 3 || line[section_length - 1] != ']') {
                fclose(file);
                return MP_LOG_STATUS_CONFIG_ERROR;
            }
            line[section_length - 1] = '\0';
            mp_logger_copy_trimmed(
                current_section,
                sizeof(current_section),
                line + 1);
            continue;
        }

        separator = strchr(line, '=');
        if (separator == NULL) {
            fclose(file);
            return MP_LOG_STATUS_CONFIG_ERROR;
        }
        *separator = '\0';
        mp_logger_copy_trimmed(key, sizeof(key), line);
        mp_logger_copy_trimmed(value, sizeof(value), separator + 1);
        if (key[0] == '\0') {
            fclose(file);
            return MP_LOG_STATUS_CONFIG_ERROR;
        }
        if (mp_logger_apply_value(current_section, key, value, &config) != MP_LOG_STATUS_OK) {
            fclose(file);
            return MP_LOG_STATUS_CONFIG_ERROR;
        }
    }

    fclose(file);
    *out_config = config;
    return MP_LOG_STATUS_OK;
}
