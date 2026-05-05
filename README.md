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
./scripts/benchmark.sh
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

Go callers should usually wrap this library behind a small cgo adapter so the rest of the service does not depend on C types or manual string lifetimes directly.

### Start from a bootstrap config file

```c
#include "mp_logger.h"

int main(void) {
    mp_logger_t *logger = NULL;

    if (mp_logger_create_from_bootstrap("configs/logger.bootstrap.ini", &logger) != MP_LOG_STATUS_OK) {
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

### Call the logger from Go with cgo

Adjust the `#cgo` include and library paths for your build layout. These examples keep all C string allocation and logger lifecycle handling inside a narrow Go adapter.

#### Start from a bootstrap config file

```go
package main

/*
#cgo CFLAGS: -I${SRCDIR}/include
#cgo LDFLAGS: -L${SRCDIR} -lmp_logger -lpthread
#include "mp_logger.h"
#include <stdlib.h>
*/
import "C"

import (
	"fmt"
	"unsafe"
)

func statusText(status C.mp_log_status_t) string {
	return C.GoString(C.mp_log_status_name(status))
}

func main() {
	configPath := C.CString("configs/logger.bootstrap.ini")
	defer C.free(unsafe.Pointer(configPath))

	var logger *C.mp_logger_t
	status := C.mp_logger_create_from_bootstrap(configPath, &logger)
	if status != C.MP_LOG_STATUS_OK {
		panic(fmt.Sprintf("create_from_bootstrap failed: %s", statusText(status)))
	}
	defer C.mp_logger_destroy(logger)

	message := C.CString("service started")
	context := C.CString("port=8080")
	defer C.free(unsafe.Pointer(message))
	defer C.free(unsafe.Pointer(context))

	status = C.mp_logger_log(logger, C.MP_LOG_LEVEL_INFO, message, context)
	if status != C.MP_LOG_STATUS_OK {
		panic(fmt.Sprintf("log failed: %s", statusText(status)))
	}
	_ = C.mp_logger_flush(logger, 2000)
	_ = C.mp_logger_shutdown(logger, 2000)
}
```

#### Build a logger in code

```go
package main

/*
#cgo CFLAGS: -I${SRCDIR}/include
#cgo LDFLAGS: -L${SRCDIR} -lmp_logger -lpthread
#include "mp_logger.h"
#include <string.h>

static void init_payments_config(mp_logger_config_t *config) {
    mp_logger_config_init_defaults(config);
    config->buffer_capacity = 1024u;
    config->message_capacity = 256u;
    config->context_capacity = 256u;
    config->format = MP_LOG_FORMAT_JSON;
    config->pretty_output = 0;
    strncpy(config->service_name, "payments", sizeof(config->service_name) - 1u);
    strncpy(config->environment_name, "prod", sizeof(config->environment_name) - 1u);
    strncpy(config->active_streams, "stdout,file", sizeof(config->active_streams) - 1u);
    strncpy(config->log_directory, "./logs", sizeof(config->log_directory) - 1u);
    strncpy(config->file_name_prefix, "payments", sizeof(config->file_name_prefix) - 1u);
    strncpy(config->backup_file_name_prefix, "payments-internal", sizeof(config->backup_file_name_prefix) - 1u);
}
*/
import "C"

import (
	"fmt"
	"unsafe"
)

func statusText(status C.mp_log_status_t) string {
	return C.GoString(C.mp_log_status_name(status))
}

func main() {
	var config C.mp_logger_config_t
	var logger *C.mp_logger_t

	C.init_payments_config(&config)

	status := C.mp_logger_create(&config, &logger)
	if status != C.MP_LOG_STATUS_OK {
		panic(fmt.Sprintf("create failed: %s", statusText(status)))
	}
	defer C.mp_logger_destroy(logger)

	if status = C.mp_logger_start(logger); status != C.MP_LOG_STATUS_OK {
		panic(fmt.Sprintf("start failed: %s", statusText(status)))
	}

	message := C.CString("retrying downstream call")
	context := C.CString("attempt=2")
	defer C.free(unsafe.Pointer(message))
	defer C.free(unsafe.Pointer(context))

	status = C.mp_logger_log(logger, C.MP_LOG_LEVEL_WARNING, message, context)
	if status != C.MP_LOG_STATUS_OK {
		panic(fmt.Sprintf("log failed: %s", statusText(status)))
	}
	_ = C.mp_logger_shutdown(logger, 2000)
}
```

## Benchmark Results

Run `./scripts/benchmark.sh` to rebuild the benchmark target in `Release` mode and refresh `.tmp/reports/benchmark-results.md`.

The results below were captured on `2026-05-05 17:21:26Z` on `Linux 7.0.2-2-cachyos x86_64` with an `AMD Ryzen AI 9 365 w/ Radeon 880M` and `20` online CPUs.

### File stream throughput

This benchmark uses the built-in file sink with the default JSON formatter. The typical payload is an `86` byte message plus a `53` byte context string. Each run uses a single producer thread for `1.5` seconds and then flushes the queue.

| Active file streams | Attempted | Processed to file | Processed logs/s | File write ops/s | Busy drops | Full drops |
|--------------------:|----------:|------------------:|-----------------:|-----------------:|-----------:|-----------:|
| `1` | 42,757,855 | 609,655 | 406,437 | 406,437 | 662,713 | 41,485,487 |
| `2` | 40,344,347 | 444,989 | 296,659 | 593,319 | 532,694 | 39,366,664 |
| `4` | 44,330,867 | 317,835 | 211,890 | 847,560 | 454,502 | 43,558,530 |
| `8` | 41,016,013 | 194,518 | 129,679 | 1,037,429 | 218,551 | 40,602,944 |

The logger supports up to `8` active streams. Those writes are not parallelized across workers: one drain thread fans each record out to each stream sequentially, so higher stream counts reduce record throughput while increasing total sink write operations per second.

### Buffer footprint by configuration

These figures are derived from the current implementation layout: `buffer_capacity * (sizeof(slot) + message_capacity + context_capacity)` for queue storage, plus worker scratch buffers.

| Profile | Slots | Message cap | Context cap | Queue reserved bytes | Worker scratch bytes | Total reserved bytes |
|:--------|------:|------------:|------------:|---------------------:|---------------------:|---------------------:|
| `small` | 64 | 64 | 128 | 15,872 | 768 | 16,640 |
| `default` | 1024 | 512 | 1024 | 1,630,208 | 3,456 | 1,633,664 |
| `deep` | 4096 | 512 | 1024 | 6,520,832 | 3,456 | 6,524,288 |

### When the buffer becomes full

Before the worker starts, the queue fills deterministically on the first call after `buffer_capacity`.

| Profile | Accepted before first failure | Failure status | Queue becomes full on call |
|:--------|------------------------------:|:---------------|---------------------------:|
| `small` | 64 | `QUEUE_FULL` | 65 |
| `default` | 1024 | `QUEUE_FULL` | 1025 |
| `deep` | 4096 | `QUEUE_FULL` | 4097 |

With the worker running behind a synthetic `500us` sink delay and a `128`-slot buffer, the benchmark found:

| Buffer slots | Highest rate without `QUEUE_FULL` | First rate with `QUEUE_FULL` | Busy drops at first full rate | Full drops at first full rate |
|-------------:|----------------------------------:|------------------------------:|------------------------------:|------------------------------:|
| 128 | 1,868 logs/s | 1,869 logs/s | 2 | 21 |

### Concurrent producer throughput

This benchmark uses a fast custom sink and drives the logger for `1.5` seconds per run. It measures how many log calls were accepted while the logger was saturated, plus which overflow mode dominated.

| Producer threads | Attempted | Accepted | Accepted logs/s | Busy drops | Full drops |
|-----------------:|----------:|---------:|----------------:|-----------:|-----------:|
| 1 | 31,680,874 | 915,485 | 610,323 | 432,253 | 30,333,136 |
| 2 | 20,773,510 | 604,725 | 403,150 | 1,910,912 | 18,257,873 |
| 4 | 20,639,962 | 520,350 | 346,900 | 7,317,186 | 12,802,426 |
| 8 | 25,416,434 | 320,696 | 213,797 | 16,553,058 | 8,542,680 |
| 16 | 39,522,416 | 180,428 | 120,285 | 34,009,000 | 5,332,988 |

### Overflow mechanisms under test

- `mp_logger_log()` returns `MP_LOG_STATUS_BUSY` when `pthread_mutex_trylock()` loses queue mutex contention.
- `mp_logger_log()` returns `MP_LOG_STATUS_QUEUE_FULL` when `queue_count` reaches `buffer_capacity`.
- `mp_logger_get_stats()` exposes cumulative `dropped_busy` and `dropped_full` counters.
- The worker mirrors drop counter growth to the backup log file with warning lines such as `log call contention dropped records` and `buffer saturation dropped records`.
