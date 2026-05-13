#define _POSIX_C_SOURCE 200809L

#include "mp_logger_internal.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* Append one character to a bounded render buffer while still tracking full logical length. */
static size_t mp_logger_append_char(char *buffer, size_t capacity, size_t used, char value) {
    if (capacity > 0 && used + 1 < capacity) {
        buffer[used] = value;
    }
    return used + 1;
}

/* Append one NUL-terminated string to a bounded render buffer. */
static size_t mp_logger_append_string(
    char *buffer,
    size_t capacity,
    size_t used,
    const char *value) {
    size_t index = 0;
    if (value == NULL) {
        return used;
    }
    while (value[index] != '\0') {
        used = mp_logger_append_char(buffer, capacity, used, value[index]);
        index++;
    }
    return used;
}

/* Copy one path component into a bounded destination while tracking the next offset. */
static int mp_logger_copy_component(
    char *buffer,
    size_t buffer_capacity,
    size_t *offset,
    const char *value) {
    size_t length = 0;
    if (buffer == NULL || offset == NULL || value == NULL) {
        return 0;
    }
    length = strlen(value);
    if (*offset + length >= buffer_capacity) {
        return 0;
    }
    memcpy(buffer + *offset, value, length);
    *offset += length;
    return 1;
}

/* Escape control characters as JSON sequences so records cannot break the rendered object shape. */
static size_t mp_logger_append_json_escaped(
    char *buffer,
    size_t capacity,
    size_t used,
    const char *value) {
    size_t index = 0;
    if (value == NULL) {
        return used;
    }
    while (value[index] != '\0') {
        unsigned char current = (unsigned char)value[index];
        switch (current) {
        case '\\':
            used = mp_logger_append_string(buffer, capacity, used, "\\\\");
            break;
        case '"':
            used = mp_logger_append_string(buffer, capacity, used, "\\\"");
            break;
        case '\n':
            used = mp_logger_append_string(buffer, capacity, used, "\\n");
            break;
        case '\r':
            used = mp_logger_append_string(buffer, capacity, used, "\\r");
            break;
        case '\t':
            used = mp_logger_append_string(buffer, capacity, used, "\\t");
            break;
        default:
            if (current < 0x20u) {
                char escape[7];
                (void)snprintf(escape, sizeof(escape), "\\u%04x", current);
                used = mp_logger_append_string(buffer, capacity, used, escape);
            } else {
                used = mp_logger_append_char(buffer, capacity, used, (char)current);
            }
            break;
        }
        index++;
    }
    return used;
}

/* Escape text output conservatively so control characters cannot forge extra text log lines. */
static size_t mp_logger_append_text_escaped(
    char *buffer,
    size_t capacity,
    size_t used,
    const char *value) {
    size_t index = 0;
    if (value == NULL) {
        return used;
    }
    while (value[index] != '\0') {
        unsigned char current = (unsigned char)value[index];
        switch (current) {
        case '\\':
            used = mp_logger_append_string(buffer, capacity, used, "\\\\");
            break;
        case '"':
            used = mp_logger_append_string(buffer, capacity, used, "\\\"");
            break;
        case '\n':
            used = mp_logger_append_string(buffer, capacity, used, "\\n");
            break;
        case '\r':
            used = mp_logger_append_string(buffer, capacity, used, "\\r");
            break;
        case '\t':
            used = mp_logger_append_string(buffer, capacity, used, "\\t");
            break;
        default:
            if (iscntrl(current)) {
                used = mp_logger_append_char(buffer, capacity, used, '?');
            } else {
                used = mp_logger_append_char(buffer, capacity, used, (char)current);
            }
            break;
        }
        index++;
    }
    return used;
}

/* Render one structured field value using JSON-native typing rules. */
static size_t mp_logger_append_json_field_value(
    char *buffer,
    size_t capacity,
    size_t used,
    const mp_log_field_t *field) {
    char number_text[64];
    if (field == NULL) {
        return used;
    }
    switch (field->type) {
    case MP_LOG_FIELD_STRING:
        used = mp_logger_append_char(buffer, capacity, used, '"');
        used = mp_logger_append_json_escaped(
            buffer,
            capacity,
            used,
            field->value.string_value == NULL ? "" : field->value.string_value);
        used = mp_logger_append_char(buffer, capacity, used, '"');
        break;
    case MP_LOG_FIELD_BOOL:
        used = mp_logger_append_string(
            buffer,
            capacity,
            used,
            field->value.bool_value ? "true" : "false");
        break;
    case MP_LOG_FIELD_INT64:
        (void)snprintf(number_text, sizeof(number_text), "%lld", (long long)field->value.int64_value);
        used = mp_logger_append_string(buffer, capacity, used, number_text);
        break;
    case MP_LOG_FIELD_UINT64:
        (void)snprintf(number_text, sizeof(number_text), "%llu", (unsigned long long)field->value.uint64_value);
        used = mp_logger_append_string(buffer, capacity, used, number_text);
        break;
    case MP_LOG_FIELD_FLOAT64:
        (void)snprintf(number_text, sizeof(number_text), "%.17g", field->value.float64_value);
        used = mp_logger_append_string(buffer, capacity, used, number_text);
        break;
    }
    return used;
}

/* Render one structured field value for the single-line text format. */
static size_t mp_logger_append_text_field_value(
    char *buffer,
    size_t capacity,
    size_t used,
    const mp_log_field_t *field) {
    char number_text[64];
    if (field == NULL) {
        return used;
    }
    switch (field->type) {
    case MP_LOG_FIELD_STRING:
        used = mp_logger_append_char(buffer, capacity, used, '"');
        used = mp_logger_append_text_escaped(
            buffer,
            capacity,
            used,
            field->value.string_value == NULL ? "" : field->value.string_value);
        used = mp_logger_append_char(buffer, capacity, used, '"');
        break;
    case MP_LOG_FIELD_BOOL:
        used = mp_logger_append_string(
            buffer,
            capacity,
            used,
            field->value.bool_value ? "true" : "false");
        break;
    case MP_LOG_FIELD_INT64:
        (void)snprintf(number_text, sizeof(number_text), "%lld", (long long)field->value.int64_value);
        used = mp_logger_append_string(buffer, capacity, used, number_text);
        break;
    case MP_LOG_FIELD_UINT64:
        (void)snprintf(number_text, sizeof(number_text), "%llu", (unsigned long long)field->value.uint64_value);
        used = mp_logger_append_string(buffer, capacity, used, number_text);
        break;
    case MP_LOG_FIELD_FLOAT64:
        (void)snprintf(number_text, sizeof(number_text), "%.17g", field->value.float64_value);
        used = mp_logger_append_string(buffer, capacity, used, number_text);
        break;
    }
    return used;
}

/* Clamp the logical length to capacity and terminate the rendered buffer. */
static void mp_logger_finalize_buffer(char *buffer, size_t capacity, size_t used) {
    if (buffer == NULL || capacity == 0) {
        return;
    }
    if (used >= capacity) {
        used = capacity - 1;
    }
    buffer[used] = '\0';
}

/* Build either ./prefix.suffix.log or directory/prefix.suffix.log without heap allocation. */
static int mp_logger_make_stream_path(
    char *buffer,
    size_t buffer_capacity,
    const char *directory,
    const char *prefix,
    const char *run_suffix) {
    size_t required_length = 0;
    size_t offset = 0;
    if (buffer == NULL || directory == NULL || prefix == NULL || run_suffix == NULL) {
        return 0;
    }
    if (strcmp(directory, ".") == 0) {
        required_length = strlen(prefix) + 1u + strlen(run_suffix) + 4u;
        if (required_length >= buffer_capacity) {
            buffer[0] = '\0';
            return 0;
        }
        if (!mp_logger_copy_component(buffer, buffer_capacity, &offset, prefix) ||
            !mp_logger_copy_component(buffer, buffer_capacity, &offset, ".") ||
            !mp_logger_copy_component(buffer, buffer_capacity, &offset, run_suffix) ||
            !mp_logger_copy_component(buffer, buffer_capacity, &offset, ".log")) {
            buffer[0] = '\0';
            return 0;
        }
        buffer[offset] = '\0';
        return 1;
    }
    required_length = strlen(directory) + 1u + strlen(prefix) + 1u + strlen(run_suffix) + 4u;
    if (required_length >= buffer_capacity) {
        buffer[0] = '\0';
        return 0;
    }
    if (!mp_logger_copy_component(buffer, buffer_capacity, &offset, directory) ||
        !mp_logger_copy_component(buffer, buffer_capacity, &offset, "/") ||
        !mp_logger_copy_component(buffer, buffer_capacity, &offset, prefix) ||
        !mp_logger_copy_component(buffer, buffer_capacity, &offset, ".") ||
        !mp_logger_copy_component(buffer, buffer_capacity, &offset, run_suffix) ||
        !mp_logger_copy_component(buffer, buffer_capacity, &offset, ".log")) {
        buffer[0] = '\0';
        return 0;
    }
    buffer[offset] = '\0';
    return 1;
}

/* Write one rendered entry plus newline to the file sink. */
static mp_log_status_t mp_logger_write_file_stream(
    void *stream_context,
    const mp_log_record_t *record,
    const char *formatted_entry,
    size_t formatted_entry_length) {
    mp_file_stream_state_t *state = (mp_file_stream_state_t *)stream_context;
    size_t ignored = 0;
    (void)record;
    (void)formatted_entry_length;
    if (state == NULL || state->file_handle == NULL || formatted_entry == NULL) {
        return MP_LOG_STATUS_INVALID_ARGUMENT;
    }
    ignored = fwrite(formatted_entry, 1u, strlen(formatted_entry), state->file_handle);
    ignored += fwrite("\n", 1u, 1u, state->file_handle);
    (void)ignored;
    if (fflush(state->file_handle) != 0) {
        return MP_LOG_STATUS_IO_ERROR;
    }
    return MP_LOG_STATUS_OK;
}

/* Flush, close, and free one owned file sink state object. */
static void mp_logger_destroy_file_stream(void *stream_context) {
    mp_file_stream_state_t *state = (mp_file_stream_state_t *)stream_context;
    if (state == NULL) {
        return;
    }
    if (state->close_on_destroy && state->file_handle != NULL) {
        (void)fclose(state->file_handle);
    }
    free(state);
}

/* Send one rendered entry as a UDP datagram to the configured endpoint. */
static mp_log_status_t mp_logger_write_udp_stream(
    void *stream_context,
    const mp_log_record_t *record,
    const char *formatted_entry,
    size_t formatted_entry_length) {
    mp_udp_stream_state_t *state = (mp_udp_stream_state_t *)stream_context;
    ssize_t sent = 0;
    (void)record;
    if (state == NULL || formatted_entry == NULL) {
        return MP_LOG_STATUS_INVALID_ARGUMENT;
    }
    sent = sendto(
        state->socket_fd,
        formatted_entry,
        formatted_entry_length,
        0,
        (const struct sockaddr *)&state->address,
        state->address_length);
    if (sent < 0 || (size_t)sent != formatted_entry_length) {
        return MP_LOG_STATUS_IO_ERROR;
    }
    return MP_LOG_STATUS_OK;
}

/* Close and free one owned UDP sink state object. */
static void mp_logger_destroy_udp_stream(void *stream_context) {
    mp_udp_stream_state_t *state = (mp_udp_stream_state_t *)stream_context;
    if (state == NULL) {
        return;
    }
    if (state->socket_fd >= 0) {
        (void)close(state->socket_fd);
    }
    free(state);
}

/* Wrap stdout and stderr in the same file-stream callback shape used by regular file sinks. */
/* Materialize a stdout or stderr stream descriptor from the configured level bounds. */
static mp_log_status_t mp_logger_make_stdio_stream(
    const char *name,
    FILE *file_handle,
    mp_log_level_t minimum_level,
    mp_log_level_t maximum_level,
    mp_logger_stream_t *out_stream) {
    mp_file_stream_state_t *state = NULL;
    if (name == NULL || file_handle == NULL || out_stream == NULL) {
        return MP_LOG_STATUS_INVALID_ARGUMENT;
    }
    state = (mp_file_stream_state_t *)calloc(1u, sizeof(*state));
    if (state == NULL) {
        return MP_LOG_STATUS_IO_ERROR;
    }
    state->file_handle = file_handle;
    state->close_on_destroy = 0;
    mp_logger_copy_trimmed(state->path, sizeof(state->path), name);
    memset(out_stream, 0, sizeof(*out_stream));
    mp_logger_copy_trimmed(out_stream->stream_name, sizeof(out_stream->stream_name), name);
    out_stream->minimum_level = minimum_level;
    out_stream->maximum_level = maximum_level;
    out_stream->stream_context = state;
    out_stream->write = mp_logger_write_file_stream;
    out_stream->destroy = mp_logger_destroy_file_stream;
    return MP_LOG_STATUS_OK;
}

/* Create a per-run file sink after sanitizing the configured prefix and ensuring the directory exists. */
/* Open the configured file sink and wrap it in an owned stream descriptor. */
static mp_log_status_t mp_logger_make_file_stream(
    mp_logger_t *logger,
    mp_logger_stream_t *out_stream) {
    mp_file_stream_state_t *state = NULL;
    char prefix[MP_LOGGER_NAME_CAPACITY];
    if (logger == NULL || out_stream == NULL) {
        return MP_LOG_STATUS_INVALID_ARGUMENT;
    }
    if (!mp_logger_ensure_directory(logger->config.log_directory)) {
        return MP_LOG_STATUS_IO_ERROR;
    }

    state = (mp_file_stream_state_t *)calloc(1u, sizeof(*state));
    if (state == NULL) {
        return MP_LOG_STATUS_IO_ERROR;
    }
    mp_logger_copy_trimmed(prefix, sizeof(prefix), logger->config.file_name_prefix);
    mp_logger_sanitize_file_component(prefix);
    if (!mp_logger_make_stream_path(
        state->path,
        sizeof(state->path),
        logger->config.log_directory,
        prefix,
        logger->run_suffix)) {
        free(state);
        return MP_LOG_STATUS_IO_ERROR;
    }
    state->file_handle = fopen(state->path, "a");
    if (state->file_handle == NULL) {
        free(state);
        return MP_LOG_STATUS_IO_ERROR;
    }
    state->close_on_destroy = 1;

    memset(out_stream, 0, sizeof(*out_stream));
    mp_logger_copy_trimmed(out_stream->stream_name, sizeof(out_stream->stream_name), "file");
    out_stream->minimum_level = logger->config.file_min_level;
    out_stream->maximum_level = logger->config.file_max_level;
    out_stream->stream_context = state;
    out_stream->write = mp_logger_write_file_stream;
    out_stream->destroy = mp_logger_destroy_file_stream;
    return MP_LOG_STATUS_OK;
}

/* Resolve the configured UDP endpoint once during startup and keep the socket state in the stream context. */
/* Resolve the configured UDP endpoint and wrap it in an owned stream descriptor. */
static mp_log_status_t mp_logger_make_udp_stream(
    const mp_logger_t *logger,
    mp_logger_stream_t *out_stream) {
    mp_udp_stream_state_t *state = NULL;
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    char port_text[16];
    int socket_fd = -1;
    if (logger == NULL || out_stream == NULL) {
        return MP_LOG_STATUS_INVALID_ARGUMENT;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    (void)snprintf(port_text, sizeof(port_text), "%u", logger->config.udp_port);
    if (getaddrinfo(logger->config.udp_host, port_text, &hints, &addresses) != 0) {
        return MP_LOG_STATUS_IO_ERROR;
    }

    socket_fd = socket(addresses->ai_family, addresses->ai_socktype, addresses->ai_protocol);
    if (socket_fd < 0) {
        freeaddrinfo(addresses);
        return MP_LOG_STATUS_IO_ERROR;
    }

    state = (mp_udp_stream_state_t *)calloc(1u, sizeof(*state));
    if (state == NULL) {
        (void)close(socket_fd);
        freeaddrinfo(addresses);
        return MP_LOG_STATUS_IO_ERROR;
    }

    state->socket_fd = socket_fd;
    memcpy(&state->address, addresses->ai_addr, addresses->ai_addrlen);
    state->address_length = (socklen_t)addresses->ai_addrlen;
    (void)snprintf(
        state->endpoint,
        sizeof(state->endpoint),
        "%s:%u",
        logger->config.udp_host,
        logger->config.udp_port);
    freeaddrinfo(addresses);

    memset(out_stream, 0, sizeof(*out_stream));
    mp_logger_copy_trimmed(out_stream->stream_name, sizeof(out_stream->stream_name), "udp");
    out_stream->minimum_level = logger->config.udp_min_level;
    out_stream->maximum_level = logger->config.udp_max_level;
    out_stream->stream_context = state;
    out_stream->write = mp_logger_write_udp_stream;
    out_stream->destroy = mp_logger_destroy_udp_stream;
    return MP_LOG_STATUS_OK;
}

int64_t mp_logger_now_millis(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
        return 0;
    }
    return (int64_t)now.tv_sec * 1000 + (int64_t)(now.tv_nsec / 1000000);
}

/* Format all timestamps as UTC RFC3339-style strings with millisecond precision. */
void mp_logger_format_timestamp(int64_t unix_epoch_millis, char *buffer, size_t buffer_capacity) {
    struct tm utc_time;
    time_t seconds = (time_t)(unix_epoch_millis / 1000);
    long millis = (long)(unix_epoch_millis % 1000);
    if (buffer == NULL || buffer_capacity == 0) {
        return;
    }
    if (millis < 0) {
        millis += 1000;
        seconds -= 1;
    }
    memset(&utc_time, 0, sizeof(utc_time));
    if (gmtime_r(&seconds, &utc_time) == NULL) {
        (void)snprintf(buffer, buffer_capacity, "1970-01-01T00:00:00.000Z");
        return;
    }
    (void)snprintf(
        buffer,
        buffer_capacity,
        "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ",
        utc_time.tm_year + 1900,
        utc_time.tm_mon + 1,
        utc_time.tm_mday,
        utc_time.tm_hour,
        utc_time.tm_min,
        utc_time.tm_sec,
        millis);
}

void mp_logger_copy_truncated(
    char *dest,
    size_t dest_capacity,
    const char *src,
    size_t *out_length) {
    size_t length = 0;
    if (dest == NULL || dest_capacity == 0) {
        if (out_length != NULL) {
            *out_length = 0;
        }
        return;
    }
    dest[0] = '\0';
    if (src == NULL) {
        if (out_length != NULL) {
            *out_length = 0;
        }
        return;
    }
    length = strlen(src);
    if (length >= dest_capacity) {
        length = dest_capacity - 1;
    }
    if (length > 0) {
        memcpy(dest, src, length);
    }
    dest[length] = '\0';
    if (out_length != NULL) {
        *out_length = length;
    }
}

void mp_logger_sanitize_file_component(char *value) {
    size_t index = 0;
    if (value == NULL || value[0] == '\0') {
        return;
    }
    while (value[index] != '\0') {
        unsigned char current = (unsigned char)value[index];
        if (!(isalnum(current) || current == '-' || current == '_')) {
            value[index] = '_';
        }
        index++;
    }
}

int mp_logger_ensure_directory(const char *path) {
    struct stat info;
    if (path == NULL || path[0] == '\0' || strcmp(path, ".") == 0) {
        return 1;
    }
    if (stat(path, &info) == 0) {
        return S_ISDIR(info.st_mode);
    }
    if (mkdir(path, 0755) == 0) {
        return 1;
    }
    if (errno == EEXIST && stat(path, &info) == 0) {
        return S_ISDIR(info.st_mode);
    }
    return 0;
}

int mp_logger_level_in_range(
    mp_log_level_t level,
    mp_log_level_t minimum,
    mp_log_level_t maximum) {
    return level >= minimum && level <= maximum;
}

/*
 * Render every record from the queued fields rather than letting sinks rebuild their own view.
 * That keeps JSON/text formatting, escaping, and structured attribute handling consistent
 * everywhere.
 */
size_t mp_logger_render_record(
    const mp_logger_t *logger,
    const mp_log_record_t *record,
    char *buffer,
    size_t buffer_capacity) {
    char timestamp[MP_LOGGER_TIMESTAMP_CAPACITY];
    size_t used = 0;
    if (logger == NULL || record == NULL || buffer == NULL || buffer_capacity == 0) {
        return 0;
    }

    mp_logger_format_timestamp(record->unix_epoch_millis, timestamp, sizeof(timestamp));

    if (logger->config.format == MP_LOG_FORMAT_JSON) {
        const char *separator = logger->config.pretty_output ? ", " : ",";
        const char *colon = logger->config.pretty_output ? ": " : ":";
        used = mp_logger_append_char(buffer, buffer_capacity, used, '{');
        used = mp_logger_append_string(buffer, buffer_capacity, used, "\"ts\"");
        used = mp_logger_append_string(buffer, buffer_capacity, used, colon);
        used = mp_logger_append_char(buffer, buffer_capacity, used, '"');
        used = mp_logger_append_json_escaped(buffer, buffer_capacity, used, timestamp);
        used = mp_logger_append_char(buffer, buffer_capacity, used, '"');
        used = mp_logger_append_string(buffer, buffer_capacity, used, separator);
        used = mp_logger_append_string(buffer, buffer_capacity, used, "\"level\"");
        used = mp_logger_append_string(buffer, buffer_capacity, used, colon);
        used = mp_logger_append_char(buffer, buffer_capacity, used, '"');
        used = mp_logger_append_json_escaped(
            buffer,
            buffer_capacity,
            used,
            mp_log_level_name(record->level));
        used = mp_logger_append_char(buffer, buffer_capacity, used, '"');
        used = mp_logger_append_string(buffer, buffer_capacity, used, separator);
        used = mp_logger_append_string(buffer, buffer_capacity, used, "\"service\"");
        used = mp_logger_append_string(buffer, buffer_capacity, used, colon);
        used = mp_logger_append_char(buffer, buffer_capacity, used, '"');
        used = mp_logger_append_json_escaped(
            buffer,
            buffer_capacity,
            used,
            logger->config.service_name);
        used = mp_logger_append_char(buffer, buffer_capacity, used, '"');
        used = mp_logger_append_string(buffer, buffer_capacity, used, separator);
        used = mp_logger_append_string(buffer, buffer_capacity, used, "\"environment\"");
        used = mp_logger_append_string(buffer, buffer_capacity, used, colon);
        used = mp_logger_append_char(buffer, buffer_capacity, used, '"');
        used = mp_logger_append_json_escaped(
            buffer,
            buffer_capacity,
            used,
            logger->config.environment_name);
        used = mp_logger_append_char(buffer, buffer_capacity, used, '"');
        used = mp_logger_append_string(buffer, buffer_capacity, used, separator);
        used = mp_logger_append_string(buffer, buffer_capacity, used, "\"sequence_id\"");
        used = mp_logger_append_string(buffer, buffer_capacity, used, colon);
        used = mp_logger_append_string(buffer, buffer_capacity, used, "\"");
        {
            char sequence_text[32];
            (void)snprintf(sequence_text, sizeof(sequence_text), "%llu", (unsigned long long)record->sequence_id);
            used = mp_logger_append_string(buffer, buffer_capacity, used, sequence_text);
        }
        used = mp_logger_append_string(buffer, buffer_capacity, used, "\"");
        used = mp_logger_append_string(buffer, buffer_capacity, used, separator);
        used = mp_logger_append_string(buffer, buffer_capacity, used, "\"message\"");
        used = mp_logger_append_string(buffer, buffer_capacity, used, colon);
        used = mp_logger_append_char(buffer, buffer_capacity, used, '"');
        used = mp_logger_append_json_escaped(buffer, buffer_capacity, used, record->message);
        used = mp_logger_append_char(buffer, buffer_capacity, used, '"');
        if (record->context_text != NULL && record->context_text[0] != '\0') {
            used = mp_logger_append_string(buffer, buffer_capacity, used, separator);
            used = mp_logger_append_string(buffer, buffer_capacity, used, "\"context\"");
            used = mp_logger_append_string(buffer, buffer_capacity, used, colon);
            used = mp_logger_append_char(buffer, buffer_capacity, used, '"');
            used = mp_logger_append_json_escaped(
                buffer,
                buffer_capacity,
                used,
                record->context_text);
            used = mp_logger_append_char(buffer, buffer_capacity, used, '"');
        }
        if (record->fields != NULL) {
            size_t index = 0;
            for (index = 0; index < record->field_count; index++) {
                const mp_log_field_t *field = &record->fields[index];
                used = mp_logger_append_string(buffer, buffer_capacity, used, separator);
                used = mp_logger_append_char(buffer, buffer_capacity, used, '"');
                used = mp_logger_append_json_escaped(buffer, buffer_capacity, used, field->key);
                used = mp_logger_append_char(buffer, buffer_capacity, used, '"');
                used = mp_logger_append_string(buffer, buffer_capacity, used, colon);
                used = mp_logger_append_json_field_value(buffer, buffer_capacity, used, field);
            }
        }
        used = mp_logger_append_char(buffer, buffer_capacity, used, '}');
    } else {
        used = mp_logger_append_string(buffer, buffer_capacity, used, timestamp);
        used = mp_logger_append_string(buffer, buffer_capacity, used, " level=");
        used = mp_logger_append_string(
            buffer,
            buffer_capacity,
            used,
            mp_log_level_name(record->level));
        used = mp_logger_append_string(buffer, buffer_capacity, used, " service=\"");
        used = mp_logger_append_text_escaped(
            buffer,
            buffer_capacity,
            used,
            logger->config.service_name);
        used = mp_logger_append_string(buffer, buffer_capacity, used, "\" environment=\"");
        used = mp_logger_append_text_escaped(
            buffer,
            buffer_capacity,
            used,
            logger->config.environment_name);
        used = mp_logger_append_string(buffer, buffer_capacity, used, "\" sequence_id=\"");
        {
            char sequence_text[32];
            (void)snprintf(sequence_text, sizeof(sequence_text), "%llu", (unsigned long long)record->sequence_id);
            used = mp_logger_append_string(buffer, buffer_capacity, used, sequence_text);
        }
        used = mp_logger_append_string(buffer, buffer_capacity, used, "\" message=\"");
        used = mp_logger_append_text_escaped(buffer, buffer_capacity, used, record->message);
        used = mp_logger_append_char(buffer, buffer_capacity, used, '"');
        if (record->context_text != NULL && record->context_text[0] != '\0') {
            used = mp_logger_append_string(buffer, buffer_capacity, used, " context=\"");
            used = mp_logger_append_text_escaped(
                buffer,
                buffer_capacity,
                used,
                record->context_text);
            used = mp_logger_append_char(buffer, buffer_capacity, used, '"');
        }
        if (record->fields != NULL) {
            size_t index = 0;
            for (index = 0; index < record->field_count; index++) {
                const mp_log_field_t *field = &record->fields[index];
                used = mp_logger_append_char(buffer, buffer_capacity, used, ' ');
                used = mp_logger_append_string(buffer, buffer_capacity, used, field->key);
                used = mp_logger_append_char(buffer, buffer_capacity, used, '=');
                used = mp_logger_append_text_field_value(buffer, buffer_capacity, used, field);
            }
        }
    }

    mp_logger_finalize_buffer(buffer, buffer_capacity, used);
    return strlen(buffer);
}

/* Open the internal backup file before primary streams so logger-internal warnings always have a sink. */
mp_log_status_t mp_logger_backup_open(mp_logger_t *logger) {
    char prefix[MP_LOGGER_NAME_CAPACITY];
    if (logger == NULL) {
        return MP_LOG_STATUS_INVALID_ARGUMENT;
    }
    if (!mp_logger_ensure_directory(logger->config.log_directory)) {
        return MP_LOG_STATUS_IO_ERROR;
    }

    mp_logger_copy_trimmed(prefix, sizeof(prefix), logger->config.backup_file_name_prefix);
    mp_logger_sanitize_file_component(prefix);
    if (!mp_logger_make_stream_path(
        logger->backup_logger.backup_path,
        sizeof(logger->backup_logger.backup_path),
        logger->config.log_directory,
        prefix,
        logger->run_suffix)) {
        return MP_LOG_STATUS_IO_ERROR;
    }
    logger->backup_logger.backup_fd = open(
        logger->backup_logger.backup_path,
        O_WRONLY | O_CREAT | O_APPEND,
        0644);
    if (logger->backup_logger.backup_fd < 0) {
        return MP_LOG_STATUS_IO_ERROR;
    }
    return MP_LOG_STATUS_OK;
}

void mp_logger_backup_close(mp_logger_t *logger) {
    if (logger == NULL) {
        return;
    }
    if (logger->backup_logger.backup_fd >= 0) {
        (void)close(logger->backup_logger.backup_fd);
        logger->backup_logger.backup_fd = -1;
    }
}

/* Serialize backup writes because worker and producer-side warning paths can race each other. */
void mp_logger_backup_write(mp_logger_t *logger, const char *severity, const char *message) {
    char line[512];
    char timestamp[MP_LOGGER_TIMESTAMP_CAPACITY];
    size_t length = 0;
    if (logger == NULL || severity == NULL || message == NULL) {
        return;
    }
    if (logger->backup_logger.backup_fd < 0) {
        return;
    }
    mp_logger_format_timestamp(mp_logger_now_millis(), timestamp, sizeof(timestamp));
    (void)snprintf(
        line,
        sizeof(line),
        "%s level=%s subsystem=mp_logger message=\"%s\"\n",
        timestamp,
        severity,
        message);
    length = strlen(line);
    (void)pthread_mutex_lock(&logger->backup_logger.mutex);
    (void)write(logger->backup_logger.backup_fd, line, length);
    (void)pthread_mutex_unlock(&logger->backup_logger.mutex);
}

/* Destroy every registered stream under the stream mutex so callbacks cannot race teardown. */
void mp_logger_destroy_streams(mp_logger_t *logger) {
    size_t index = 0;
    if (logger == NULL) {
        return;
    }
    (void)pthread_mutex_lock(&logger->stream_mutex);
    for (index = 0; index < logger->stream_count; index++) {
        if (logger->streams[index].destroy != NULL) {
            logger->streams[index].destroy(logger->streams[index].stream_context);
        }
        memset(&logger->streams[index], 0, sizeof(logger->streams[index]));
    }
    logger->stream_count = 0;
    atomic_store(&logger->enabled_level_mask, 0u);
    (void)pthread_mutex_unlock(&logger->stream_mutex);
}

/*
 * Build the configured builtin stream set in declaration order.
 * Individual builtin initialization failures are mirrored to the backup logger so startup remains
 * observable even when only part of the configured sink set can be activated.
 */
mp_log_status_t mp_logger_build_builtin_streams(mp_logger_t *logger) {
    char names[MP_LOGGER_MAX_STREAMS][32];
    size_t name_count = 0;
    size_t index = 0;
    if (logger == NULL) {
        return MP_LOG_STATUS_INVALID_ARGUMENT;
    }
    if (!mp_logger_split_stream_list(
            logger->config.active_streams,
            names,
            MP_LOGGER_MAX_STREAMS,
            &name_count)) {
        return MP_LOG_STATUS_CONFIG_ERROR;
    }
    for (index = 0; index < name_count; index++) {
        mp_logger_stream_t stream;
        mp_log_status_t status = MP_LOG_STATUS_CONFIG_ERROR;
        memset(&stream, 0, sizeof(stream));
        if (strcmp(names[index], "stdout") == 0) {
            status = mp_logger_make_stdio_stream(
                "stdout",
                stdout,
                logger->config.stdout_min_level,
                logger->config.stdout_max_level,
                &stream);
        } else if (strcmp(names[index], "stderr") == 0) {
            status = mp_logger_make_stdio_stream(
                "stderr",
                stderr,
                logger->config.stderr_min_level,
                logger->config.stderr_max_level,
                &stream);
        } else if (strcmp(names[index], "file") == 0) {
            status = mp_logger_make_file_stream(logger, &stream);
        } else if (strcmp(names[index], "udp") == 0) {
            status = mp_logger_make_udp_stream(logger, &stream);
        } else {
            return MP_LOG_STATUS_CONFIG_ERROR;
        }

        if (status != MP_LOG_STATUS_OK) {
            mp_logger_backup_write(logger, "WARNING", "builtin stream initialization failed");
            continue;
        }
        status = mp_logger_add_owned_stream(logger, &stream);
        if (status != MP_LOG_STATUS_OK && stream.destroy != NULL) {
            stream.destroy(stream.stream_context);
        }
        if (status != MP_LOG_STATUS_OK) {
            return status;
        }
    }
    return MP_LOG_STATUS_OK;
}
