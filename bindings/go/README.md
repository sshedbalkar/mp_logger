# Go Wrapper

This package provides a thin cgo wrapper for `mp_logger` at import path `mp_logger/bindings/go`.

## Scope

- wraps config loading, logger creation, lifecycle, logging, and stats;
- compiles the vendored C sources in this repository, so no prebuilt `libmp_logger` is required;
- keeps C strings and `mp_logger_t *` ownership inside the wrapper;
- does not expose custom stream callbacks from Go.

Custom sinks remain available through the C API. The Go wrapper intentionally stays narrow so the rest of a Go service can avoid direct C types.

## Requirements

- `CGO_ENABLED=1`
- a C toolchain with pthread support

## Commands

```bash
cd bindings/go
go test ./...
```

## Example

```go
package main

import (
	"log"
	"time"

	mplogger "mp_logger/bindings/go"
)

func main() {
	logger, err := mplogger.CreateFromBootstrap("configs/logger.bootstrap.ini")
	if err != nil {
		log.Fatal(err)
	}
	defer logger.Close()

	if err := logger.Log(mplogger.Info, "service started", "port=8080"); err != nil {
		log.Fatal(err)
	}
	if err := logger.Flush(2 * time.Second); err != nil {
		log.Fatal(err)
	}
	if err := logger.Shutdown(2 * time.Second); err != nil {
		log.Fatal(err)
	}
}
```
