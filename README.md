# mp_logger

Standalone C logging library for low-latency services and tools.

## Features

- non-blocking log calls backed by a bounded in-memory buffer;
- concurrent producer support with a dedicated worker thread;
- levels: `TRACE`, `DEBUG`, `INFO`, `WARNING`, `ERROR`, `FATAL`;
- default streams: `stdout`, `stderr`, and a per-run local file;
- pluggable stream interface for custom sinks;
- built-in UDP stream support through bootstrap config;
- separate backup file logger for logger-internal warnings and errors;
- CMake build, CTest coverage, installable library packaging, and static plus LLM validation scripts.

## Layout

```text
include/                 public header
src/                     implementation
tests/                   C unit tests
configs/                 bootstrap config template
docs/                    extracted standards and architecture docs
context/                 dedicated retrieval and validation indexes
scripts/                 build, test, deploy, and validator entrypoints
```

## Commands

```bash
./scripts/build.sh
./scripts/test.sh
./scripts/deploy.sh
./scripts/validate-static.sh
./scripts/validate-llm.sh
```

## Bootstrap Config

Template: [configs/logger.bootstrap.ini](configs/logger.bootstrap.ini)

The bootstrap loader accepts root keys plus `[logger]`, `[stdout]`, `[stderr]`, `[file]`, and `[udp]` sections. Unknown sections, unknown keys, or malformed values are rejected with `MP_LOG_STATUS_CONFIG_ERROR`.

### Root keys

| Key | Type | Default | Notes |
|:----|:-----|:--------|:------|
| `service_name` | string | `mp-service` | Included in every rendered record. Must not be empty. |
| `environment_name` | string | `development` | Included in every rendered record. Must not be empty. |

### `[logger]` keys

| Key | Type | Default | Notes |
|:----|:-----|:--------|:------|
| `buffer_capacity` | integer | `1024` | Queue slot count. Must be greater than `0`. |
| `message_capacity` | integer | `512` | Per-record message buffer size. Must be greater than `1`. |
| `context_capacity` | integer | `1024` | Per-record context buffer size. Must be greater than `1`. |
| `format` | enum | `json` | Supported values: `text`, `json`. |
| `pretty_output` | boolean | `false` | Accepted values: `true`, `false`, `yes`, `no`, `1`, `0`. |
| `log_directory` | string | `.` | Directory used for per-run primary and backup log files. Must not be empty. |
| `file_name_prefix` | string | `mp-service` | Prefix for the primary log file name. Must not be empty. |
| `backup_file_name_prefix` | string | `mp-logger-internal` | Prefix for logger-internal backup log files. Must not be empty. |
| `active_streams` | comma-separated string | `stdout,stderr,file` | Built-in names: `stdout`, `stderr`, `file`, `udp`. Empty disables built-in streams. |

### `[stdout]` keys

| Key | Type | Default | Notes |
|:----|:-----|:--------|:------|
| `minimum_level` | enum | `trace` | Supported levels: `trace`, `debug`, `info`, `warning`, `error`, `fatal`. `warn` is also accepted. |
| `maximum_level` | enum | `info` | Same accepted values as `minimum_level`. |

### `[stderr]` keys

| Key | Type | Default | Notes |
|:----|:-----|:--------|:------|
| `minimum_level` | enum | `warning` | Supported levels: `trace`, `debug`, `info`, `warning`, `error`, `fatal`. `warn` is also accepted. |
| `maximum_level` | enum | `fatal` | Same accepted values as `minimum_level`. |

### `[file]` keys

| Key | Type | Default | Notes |
|:----|:-----|:--------|:------|
| `minimum_level` | enum | `trace` | Supported levels: `trace`, `debug`, `info`, `warning`, `error`, `fatal`. `warn` is also accepted. |
| `maximum_level` | enum | `fatal` | Same accepted values as `minimum_level`. |

### `[udp]` keys

| Key | Type | Default | Notes |
|:----|:-----|:--------|:------|
| `minimum_level` | enum | `error` | Supported levels: `trace`, `debug`, `info`, `warning`, `error`, `fatal`. `warn` is also accepted. |
| `maximum_level` | enum | `fatal` | Same accepted values as `minimum_level`. |
| `host` | string | `127.0.0.1` | Destination host for the UDP sink. |
| `port` | integer | `5514` | Destination UDP port. Must fit in `uint16_t`. |

## Public API

- `mp_logger_bootstrap_load()` loads the bootstrap config file.
- `mp_logger_create()` creates a logger from config without starting the worker thread.
- `mp_logger_add_stream()` registers a custom sink before or after startup.
- `mp_logger_start()` starts the drain worker.
- `mp_logger_log()` enqueues a log entry without blocking on sink I/O.
- `mp_logger_flush()` waits for queued work to drain.
- `mp_logger_shutdown()` stops the worker after draining the queue.

Custom streams receive both the structured record and the already-rendered log line. The logger owns stream teardown only when a `destroy` callback is supplied.

## Usage Examples

### Start from a bootstrap config file

```c
#include "mp_logger.h"

int main(void) {
    mp_logger_t *logger = NULL;

    if (mp_logger_create_from_bootstrap("configs/logger.bootstrap.ini", &logger) != MP_LOG_STATUS_OK) {
        return 1;
    }
    if (mp_logger_start(logger) != MP_LOG_STATUS_OK) {
        mp_logger_destroy(logger);
        return 1;
    }
    (void)mp_logger_log(logger, MP_LOG_LEVEL_INFO, "service started", "port=8080");
    (void)mp_logger_flush(logger, 2000u);
    (void)mp_logger_shutdown(logger, 2000u);
    mp_logger_destroy(logger);
    return 0;
}
```

### Build a logger in code

```c
#include "mp_logger.h"

#include <stdio.h>

int main(void) {
    mp_logger_config_t config;
    mp_logger_t *logger = NULL;

    mp_logger_config_init_defaults(&config);
    config.buffer_capacity = 1024u;
    config.message_capacity = 256u;
    config.context_capacity = 256u;
    config.format = MP_LOG_FORMAT_JSON;
    config.pretty_output = 0;
    (void)snprintf(config.service_name, sizeof(config.service_name), "%s", "payments");
    (void)snprintf(config.environment_name, sizeof(config.environment_name), "%s", "prod");
    (void)snprintf(config.active_streams, sizeof(config.active_streams), "%s", "stdout,file");
    (void)snprintf(config.log_directory, sizeof(config.log_directory), "%s", "./logs");
    (void)snprintf(config.file_name_prefix, sizeof(config.file_name_prefix), "%s", "payments");
    (void)snprintf(config.backup_file_name_prefix, sizeof(config.backup_file_name_prefix), "%s", "payments-internal");

    if (mp_logger_create(&config, &logger) != MP_LOG_STATUS_OK) {
        return 1;
    }
    if (mp_logger_start(logger) != MP_LOG_STATUS_OK) {
        mp_logger_destroy(logger);
        return 1;
    }
    (void)mp_logger_log(logger, MP_LOG_LEVEL_WARNING, "retrying downstream call", "attempt=2");
    (void)mp_logger_shutdown(logger, 2000u);
    mp_logger_destroy(logger);
    return 0;
}
```

### Register a custom stream

```c
#include "mp_logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FILE *file;
} audit_stream_t;

static mp_log_status_t audit_stream_write(
    void *stream_context,
    const mp_log_record_t *record,
    const char *formatted_entry,
    size_t formatted_entry_length) {
    audit_stream_t *audit = (audit_stream_t *)stream_context;
    (void)record;
    if (audit == NULL || audit->file == NULL || formatted_entry == NULL) {
        return MP_LOG_STATUS_INVALID_ARGUMENT;
    }
    return fwrite(formatted_entry, 1u, formatted_entry_length, audit->file) == formatted_entry_length
        ? MP_LOG_STATUS_OK
        : MP_LOG_STATUS_IO_ERROR;
}

static void audit_stream_destroy(void *stream_context) {
    audit_stream_t *audit = (audit_stream_t *)stream_context;
    if (audit != NULL && audit->file != NULL) {
        (void)fclose(audit->file);
    }
}

int main(void) {
    mp_logger_config_t config;
    mp_logger_stream_t stream;
    mp_logger_t *logger = NULL;
    audit_stream_t *audit = NULL;

    mp_logger_config_init_defaults(&config);
    (void)snprintf(config.active_streams, sizeof(config.active_streams), "%s", "");

    if (mp_logger_create(&config, &logger) != MP_LOG_STATUS_OK) {
        return 1;
    }
    audit = (audit_stream_t *)calloc(1u, sizeof(*audit));
    if (audit == NULL) {
        mp_logger_destroy(logger);
        return 1;
    }
    audit->file = fopen("audit.log", "a");
    if (audit->file == NULL) {
        free(audit);
        mp_logger_destroy(logger);
        return 1;
    }

    memset(&stream, 0, sizeof(stream));
    (void)snprintf(stream.stream_name, sizeof(stream.stream_name), "%s", "audit");
    stream.minimum_level = MP_LOG_LEVEL_ERROR;
    stream.maximum_level = MP_LOG_LEVEL_FATAL;
    stream.stream_context = audit;
    stream.write = audit_stream_write;
    stream.destroy = audit_stream_destroy;

    (void)mp_logger_add_stream(logger, &stream);
    (void)mp_logger_start(logger);
    (void)mp_logger_log(logger, MP_LOG_LEVEL_ERROR, "card token rejected", "tenant=alpha");
    (void)mp_logger_shutdown(logger, 2000u);
    mp_logger_destroy(logger);
    return 0;
}
```

### Inspect queue pressure and drops

```c
#include "mp_logger.h"

#include <stdio.h>

static void print_logger_stats(mp_logger_t *logger) {
    mp_logger_stats_t stats;

    if (mp_logger_get_stats(logger, &stats) != MP_LOG_STATUS_OK) {
        return;
    }
    printf(
        "queued=%llu processed=%llu dropped_busy=%llu dropped_full=%llu streams=%zu\n",
        (unsigned long long)stats.queued_records,
        (unsigned long long)stats.processed_records,
        (unsigned long long)stats.dropped_busy,
        (unsigned long long)stats.dropped_full,
        stats.active_stream_count);
}
```
