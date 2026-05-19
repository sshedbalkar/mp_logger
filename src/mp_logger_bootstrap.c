#include "mp_logger_internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Compare two ASCII configuration tokens case-insensitively. */
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

/* Parse the accepted boolean spellings used by bootstrap config. */
static int mp_logger_parse_bool(const char *value, int *out_bool) {
    if (value == NULL || out_bool == NULL) {
        return 0;
    }
    if (mp_logger_equal_ignore_case(value, MP_LOGGER_BOOL_TRUE) ||
        strcmp(value, MP_LOGGER_BOOL_ONE) == 0 ||
        mp_logger_equal_ignore_case(value, MP_LOGGER_BOOL_YES)) {
        *out_bool = 1;
        return 1;
    }
    if (mp_logger_equal_ignore_case(value, MP_LOGGER_BOOL_FALSE) ||
        strcmp(value, MP_LOGGER_BOOL_ZERO) == 0 ||
        mp_logger_equal_ignore_case(value, MP_LOGGER_BOOL_NO)) {
        *out_bool = 0;
        return 1;
    }
    return 0;
}

/* Parse one non-negative decimal size value from bootstrap config. */
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

/* Parse one UDP port value while enforcing the uint16 range. */
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

/* Validate MAJOR.MINOR.HOTFIX text with numeric-only components. */
static int mp_logger_is_build_version_valid(const char *value) {
    unsigned int component_count = 0u;
    int saw_digit = 0;
    if (value == NULL || value[0] == '\0') {
	return 0;
    }
    while (*value != '\0') {
	if (isdigit((unsigned char)*value)) {
	    saw_digit = 1;
	} else if (*value == '.') {
	    if (!saw_digit) {
		return 0;
	    }
	    component_count++;
	    saw_digit = 0;
	} else {
	    return 0;
	}
	value++;
    }
    return saw_digit && component_count == 2u;
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

/* Apply root-level service metadata keys. */
static mp_log_status_t mp_logger_apply_root_value(
    const char *key,
    const char *value,
    mp_logger_config_t *config) {
    if (strcmp(key, MP_LOGGER_CONFIG_KEY_SERVICE_NAME) == 0) {
        mp_logger_copy_trimmed(config->service_name, sizeof(config->service_name), value);
        return MP_LOG_STATUS_OK;
    }
    if (strcmp(key, MP_LOGGER_CONFIG_KEY_ENVIRONMENT_NAME) == 0) {
        mp_logger_copy_trimmed(config->environment_name, sizeof(config->environment_name), value);
        return MP_LOG_STATUS_OK;
    }
    if (strcmp(key, MP_LOGGER_CONFIG_KEY_BUILD_VERSION) == 0) {
	if (!mp_logger_is_build_version_valid(value)) {
	    return MP_LOG_STATUS_CONFIG_ERROR;
	}
	mp_logger_copy_trimmed(config->build_version, sizeof(config->build_version), value);
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
    if (strcmp(key, MP_LOGGER_CONFIG_KEY_BUFFER_CAPACITY) == 0) {
        if (!mp_logger_parse_size_value(value, &parsed_size)) {
            return MP_LOG_STATUS_CONFIG_ERROR;
        }
        config->buffer_capacity = parsed_size;
        return MP_LOG_STATUS_OK;
    }
    if (strcmp(key, MP_LOGGER_CONFIG_KEY_MESSAGE_CAPACITY) == 0) {
        if (!mp_logger_parse_size_value(value, &parsed_size)) {
            return MP_LOG_STATUS_CONFIG_ERROR;
        }
        config->message_capacity = parsed_size;
        return MP_LOG_STATUS_OK;
    }
    if (strcmp(key, MP_LOGGER_CONFIG_KEY_CONTEXT_CAPACITY) == 0) {
        if (!mp_logger_parse_size_value(value, &parsed_size)) {
            return MP_LOG_STATUS_CONFIG_ERROR;
        }
        config->context_capacity = parsed_size;
        return MP_LOG_STATUS_OK;
    }
    if (strcmp(key, MP_LOGGER_CONFIG_KEY_FIELD_CAPACITY) == 0) {
        if (!mp_logger_parse_size_value(value, &parsed_size)) {
            return MP_LOG_STATUS_CONFIG_ERROR;
        }
        config->field_capacity = parsed_size;
        return MP_LOG_STATUS_OK;
    }
    if (strcmp(key, MP_LOGGER_CONFIG_KEY_FIELD_KEY_CAPACITY) == 0) {
        if (!mp_logger_parse_size_value(value, &parsed_size)) {
            return MP_LOG_STATUS_CONFIG_ERROR;
        }
        config->field_key_capacity = parsed_size;
        return MP_LOG_STATUS_OK;
    }
    if (strcmp(key, MP_LOGGER_CONFIG_KEY_FIELD_VALUE_CAPACITY) == 0) {
        if (!mp_logger_parse_size_value(value, &parsed_size)) {
            return MP_LOG_STATUS_CONFIG_ERROR;
        }
        config->field_value_capacity = parsed_size;
        return MP_LOG_STATUS_OK;
    }
    if (strcmp(key, MP_LOGGER_CONFIG_KEY_FORMAT) == 0) {
        return mp_logger_parse_format_name(value, &config->format);
    }
    if (strcmp(key, MP_LOGGER_CONFIG_KEY_PRETTY_OUTPUT) == 0) {
        if (!mp_logger_parse_bool(value, &parsed_bool)) {
            return MP_LOG_STATUS_CONFIG_ERROR;
        }
        config->pretty_output = parsed_bool;
        return MP_LOG_STATUS_OK;
    }
    if (strcmp(key, MP_LOGGER_CONFIG_KEY_LOG_DIRECTORY) == 0) {
        mp_logger_copy_trimmed(config->log_directory, sizeof(config->log_directory), value);
        return MP_LOG_STATUS_OK;
    }
    if (strcmp(key, MP_LOGGER_CONFIG_KEY_FILE_NAME_PREFIX) == 0) {
        mp_logger_copy_trimmed(config->file_name_prefix, sizeof(config->file_name_prefix), value);
        return MP_LOG_STATUS_OK;
    }
    if (strcmp(key, MP_LOGGER_CONFIG_KEY_BACKUP_FILE_NAME_PREFIX) == 0) {
        mp_logger_copy_trimmed(
            config->backup_file_name_prefix,
            sizeof(config->backup_file_name_prefix),
            value);
        return MP_LOG_STATUS_OK;
    }
    if (strcmp(key, MP_LOGGER_CONFIG_KEY_ACTIVE_STREAMS) == 0) {
        mp_logger_copy_trimmed(config->active_streams, sizeof(config->active_streams), value);
        return MP_LOG_STATUS_OK;
    }
    return MP_LOG_STATUS_CONFIG_ERROR;
}

/* Apply stdout stream level bounds from bootstrap config. */
static mp_log_status_t mp_logger_apply_stdout_value(
    const char *key,
    const char *value,
    mp_logger_config_t *config) {
    if (strcmp(key, MP_LOGGER_CONFIG_KEY_MINIMUM_LEVEL) == 0) {
        return mp_logger_parse_level_name(value, &config->stdout_min_level);
    }
    if (strcmp(key, MP_LOGGER_CONFIG_KEY_MAXIMUM_LEVEL) == 0) {
        return mp_logger_parse_level_name(value, &config->stdout_max_level);
    }
    return MP_LOG_STATUS_CONFIG_ERROR;
}

/* Apply stderr stream level bounds from bootstrap config. */
static mp_log_status_t mp_logger_apply_stderr_value(
    const char *key,
    const char *value,
    mp_logger_config_t *config) {
    if (strcmp(key, MP_LOGGER_CONFIG_KEY_MINIMUM_LEVEL) == 0) {
        return mp_logger_parse_level_name(value, &config->stderr_min_level);
    }
    if (strcmp(key, MP_LOGGER_CONFIG_KEY_MAXIMUM_LEVEL) == 0) {
        return mp_logger_parse_level_name(value, &config->stderr_max_level);
    }
    return MP_LOG_STATUS_CONFIG_ERROR;
}

/* Apply file stream level bounds from bootstrap config. */
static mp_log_status_t mp_logger_apply_file_value(
    const char *key,
    const char *value,
    mp_logger_config_t *config) {
    if (strcmp(key, MP_LOGGER_CONFIG_KEY_MINIMUM_LEVEL) == 0) {
        return mp_logger_parse_level_name(value, &config->file_min_level);
    }
    if (strcmp(key, MP_LOGGER_CONFIG_KEY_MAXIMUM_LEVEL) == 0) {
        return mp_logger_parse_level_name(value, &config->file_max_level);
    }
    return MP_LOG_STATUS_CONFIG_ERROR;
}

/* Apply UDP stream destination and level bounds from bootstrap config. */
static mp_log_status_t mp_logger_apply_udp_value(
    const char *key,
    const char *value,
    mp_logger_config_t *config) {
    if (strcmp(key, MP_LOGGER_CONFIG_KEY_MINIMUM_LEVEL) == 0) {
        return mp_logger_parse_level_name(value, &config->udp_min_level);
    }
    if (strcmp(key, MP_LOGGER_CONFIG_KEY_MAXIMUM_LEVEL) == 0) {
        return mp_logger_parse_level_name(value, &config->udp_max_level);
    }
    if (strcmp(key, MP_LOGGER_CONFIG_KEY_HOST) == 0) {
        mp_logger_copy_trimmed(config->udp_host, sizeof(config->udp_host), value);
        return MP_LOG_STATUS_OK;
    }
    if (strcmp(key, MP_LOGGER_CONFIG_KEY_PORT) == 0) {
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
    if (strcmp(section, MP_LOGGER_CONFIG_SECTION_ROOT) == 0) {
        return mp_logger_apply_root_value(key, value, config);
    }
    if (strcmp(section, MP_LOGGER_CONFIG_SECTION_LOGGER) == 0) {
        return mp_logger_apply_logger_value(key, value, config);
    }
    if (strcmp(section, MP_LOGGER_CONFIG_SECTION_STDOUT) == 0) {
        return mp_logger_apply_stdout_value(key, value, config);
    }
    if (strcmp(section, MP_LOGGER_CONFIG_SECTION_STDERR) == 0) {
        return mp_logger_apply_stderr_value(key, value, config);
    }
    if (strcmp(section, MP_LOGGER_CONFIG_SECTION_FILE) == 0) {
        return mp_logger_apply_file_value(key, value, config);
    }
    if (strcmp(section, MP_LOGGER_CONFIG_SECTION_UDP) == 0) {
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
    mp_logger_copy_trimmed(config->service_name, sizeof(config->service_name), MP_LOGGER_DEFAULT_SERVICE_NAME);
    mp_logger_copy_trimmed(
        config->environment_name,
        sizeof(config->environment_name),
        MP_LOGGER_DEFAULT_ENVIRONMENT_NAME);
    mp_logger_copy_trimmed(config->build_version, sizeof(config->build_version), MP_LOGGER_DEFAULT_BUILD_VERSION);
    config->buffer_capacity = MP_LOGGER_DEFAULT_BUFFER_CAPACITY;
    config->message_capacity = MP_LOGGER_DEFAULT_MESSAGE_CAPACITY;
    config->context_capacity = MP_LOGGER_DEFAULT_CONTEXT_CAPACITY;
    config->field_capacity = MP_LOGGER_DEFAULT_FIELD_CAPACITY;
    config->field_key_capacity = MP_LOGGER_DEFAULT_FIELD_KEY_CAPACITY;
    config->field_value_capacity = MP_LOGGER_DEFAULT_FIELD_VALUE_CAPACITY;
    config->format = MP_LOG_FORMAT_JSON;
    config->pretty_output = MP_LOGGER_DEFAULT_PRETTY_OUTPUT;
    mp_logger_copy_trimmed(config->log_directory, sizeof(config->log_directory), MP_LOGGER_DEFAULT_LOG_DIRECTORY);
    mp_logger_copy_trimmed(
        config->file_name_prefix,
        sizeof(config->file_name_prefix),
        MP_LOGGER_DEFAULT_FILE_NAME_PREFIX);
    mp_logger_copy_trimmed(
        config->backup_file_name_prefix,
        sizeof(config->backup_file_name_prefix),
        MP_LOGGER_DEFAULT_BACKUP_FILE_NAME_PREFIX);
    mp_logger_copy_trimmed(
        config->active_streams,
        sizeof(config->active_streams),
        MP_LOGGER_DEFAULT_ACTIVE_STREAMS);
    config->stdout_min_level = MP_LOG_LEVEL_TRACE;
    config->stdout_max_level = MP_LOG_LEVEL_INFO;
    config->stderr_min_level = MP_LOG_LEVEL_WARNING;
    config->stderr_max_level = MP_LOG_LEVEL_FATAL;
    config->file_min_level = MP_LOG_LEVEL_TRACE;
    config->file_max_level = MP_LOG_LEVEL_FATAL;
    config->udp_min_level = MP_LOG_LEVEL_ERROR;
    config->udp_max_level = MP_LOG_LEVEL_FATAL;
    mp_logger_copy_trimmed(config->udp_host, sizeof(config->udp_host), MP_LOGGER_DEFAULT_UDP_HOST);
    config->udp_port = MP_LOGGER_DEFAULT_UDP_PORT;
}

/* Accept the documented level aliases while rejecting anything the logger would render ambiguously. */
mp_log_status_t mp_logger_parse_level_name(const char *value, mp_log_level_t *out_level) {
    if (value == NULL || out_level == NULL) {
        return MP_LOG_STATUS_INVALID_ARGUMENT;
    }
    if (mp_logger_equal_ignore_case(value, MP_LOGGER_LEVEL_TOKEN_TRACE)) {
        *out_level = MP_LOG_LEVEL_TRACE;
        return MP_LOG_STATUS_OK;
    }
    if (mp_logger_equal_ignore_case(value, MP_LOGGER_LEVEL_TOKEN_DEBUG)) {
        *out_level = MP_LOG_LEVEL_DEBUG;
        return MP_LOG_STATUS_OK;
    }
    if (mp_logger_equal_ignore_case(value, MP_LOGGER_LEVEL_TOKEN_INFO)) {
        *out_level = MP_LOG_LEVEL_INFO;
        return MP_LOG_STATUS_OK;
    }
    if (mp_logger_equal_ignore_case(value, MP_LOGGER_LEVEL_TOKEN_WARN) ||
        mp_logger_equal_ignore_case(value, MP_LOGGER_LEVEL_TOKEN_WARNING)) {
        *out_level = MP_LOG_LEVEL_WARNING;
        return MP_LOG_STATUS_OK;
    }
    if (mp_logger_equal_ignore_case(value, MP_LOGGER_LEVEL_TOKEN_ERROR)) {
        *out_level = MP_LOG_LEVEL_ERROR;
        return MP_LOG_STATUS_OK;
    }
    if (mp_logger_equal_ignore_case(value, MP_LOGGER_LEVEL_TOKEN_FATAL)) {
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
    if (mp_logger_equal_ignore_case(value, MP_LOGGER_FORMAT_NAME_TEXT)) {
        *out_format = MP_LOG_FORMAT_TEXT;
        return MP_LOG_STATUS_OK;
    }
    if (mp_logger_equal_ignore_case(value, MP_LOGGER_FORMAT_NAME_JSON)) {
        *out_format = MP_LOG_FORMAT_JSON;
        return MP_LOG_STATUS_OK;
    }
    return MP_LOG_STATUS_CONFIG_ERROR;
}

/* Split the comma-separated stream list in place so builtin stream activation stays order-preserving. */
int mp_logger_split_stream_list(
    const char *active_streams,
    char names[][MP_LOGGER_STREAM_NAME_CAPACITY],
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
        mp_logger_copy_trimmed(names[count], MP_LOGGER_STREAM_NAME_CAPACITY, cursor);
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
    char line[MP_LOGGER_BOOTSTRAP_LINE_CAPACITY];
    char current_section[MP_LOGGER_BOOTSTRAP_SECTION_CAPACITY] = MP_LOGGER_CONFIG_SECTION_ROOT;
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
        char key[MP_LOGGER_BOOTSTRAP_KEY_CAPACITY];
        char value[MP_LOGGER_BOOTSTRAP_VALUE_CAPACITY];
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
