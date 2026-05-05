package mplogger

/*
#cgo CFLAGS: -std=c17 -Wall -Wextra -Wpedantic -I${SRCDIR}/../../include -I${SRCDIR}/../../src
#cgo LDFLAGS: -lpthread
#include <stdlib.h>
#include "mp_logger.h"
*/
import "C"

import (
	"errors"
	"fmt"
	"sync"
	"time"
	"unsafe"
)

const maxTimeoutMillis = int64(^uint32(0))

type Level int

const (
	Trace   Level = Level(C.MP_LOG_LEVEL_TRACE)
	Debug   Level = Level(C.MP_LOG_LEVEL_DEBUG)
	Info    Level = Level(C.MP_LOG_LEVEL_INFO)
	Warning Level = Level(C.MP_LOG_LEVEL_WARNING)
	Error   Level = Level(C.MP_LOG_LEVEL_ERROR)
	Fatal   Level = Level(C.MP_LOG_LEVEL_FATAL)
)

func (level Level) String() string {
	return C.GoString(C.mp_log_level_name(C.mp_log_level_t(level)))
}

type Format int

const (
	Text Format = Format(C.MP_LOG_FORMAT_TEXT)
	JSON Format = Format(C.MP_LOG_FORMAT_JSON)
)

type Status int

const (
	StatusOK              Status = Status(C.MP_LOG_STATUS_OK)
	StatusQueueFull       Status = Status(C.MP_LOG_STATUS_QUEUE_FULL)
	StatusBusy            Status = Status(C.MP_LOG_STATUS_BUSY)
	StatusInvalidArgument Status = Status(C.MP_LOG_STATUS_INVALID_ARGUMENT)
	StatusIOError         Status = Status(C.MP_LOG_STATUS_IO_ERROR)
	StatusConfigError     Status = Status(C.MP_LOG_STATUS_CONFIG_ERROR)
	StatusNotRunning      Status = Status(C.MP_LOG_STATUS_NOT_RUNNING)
	StatusInternalError   Status = Status(C.MP_LOG_STATUS_INTERNAL_ERROR)
	StatusLimitExceeded   Status = Status(C.MP_LOG_STATUS_LIMIT_EXCEEDED)
)

func (status Status) String() string {
	return C.GoString(C.mp_log_status_name(C.mp_log_status_t(status)))
}

type StatusError struct {
	Op     string
	Status Status
}

func (err *StatusError) Error() string {
	if err == nil {
		return "<nil>"
	}
	if err.Op == "" {
		return err.Status.String()
	}
	return fmt.Sprintf("%s: %s", err.Op, err.Status)
}

var ErrClosed = errors.New("mp_logger: logger closed")

type Config struct {
	ServiceName          string
	EnvironmentName      string
	BufferCapacity       int
	MessageCapacity      int
	ContextCapacity      int
	Format               Format
	PrettyOutput         bool
	LogDirectory         string
	FileNamePrefix       string
	BackupFileNamePrefix string
	ActiveStreams        string
	StdoutMinLevel       Level
	StdoutMaxLevel       Level
	StderrMinLevel       Level
	StderrMaxLevel       Level
	FileMinLevel         Level
	FileMaxLevel         Level
	UDPMinLevel          Level
	UDPMaxLevel          Level
	UDPHost              string
	UDPPort              uint16
}

type Stats struct {
	QueuedRecords     uint64
	ProcessedRecords  uint64
	DroppedBusy       uint64
	DroppedFull       uint64
	ActiveStreamCount int
}

type Logger struct {
	mu  sync.RWMutex
	ptr *C.mp_logger_t
}

func DefaultConfig() Config {
	var config C.mp_logger_config_t
	C.mp_logger_config_init_defaults(&config)
	return configFromC(config)
}

func LoadBootstrapConfig(path string) (Config, error) {
	cPath := C.CString(path)
	defer C.free(unsafe.Pointer(cPath))

	var config C.mp_logger_config_t
	status := C.mp_logger_bootstrap_load(cPath, &config)
	if err := statusError("bootstrap_load", status); err != nil {
		return Config{}, err
	}
	return configFromC(config), nil
}

func Create(config Config) (*Logger, error) {
	cConfig, err := config.toC()
	if err != nil {
		return nil, err
	}

	var logger *C.mp_logger_t
	status := C.mp_logger_create(&cConfig, &logger)
	if err := statusError("create", status); err != nil {
		return nil, err
	}
	return &Logger{ptr: logger}, nil
}

func CreateFromBootstrap(path string) (*Logger, error) {
	cPath := C.CString(path)
	defer C.free(unsafe.Pointer(cPath))

	var logger *C.mp_logger_t
	status := C.mp_logger_create_from_bootstrap(cPath, &logger)
	if err := statusError("create_from_bootstrap", status); err != nil {
		return nil, err
	}
	return &Logger{ptr: logger}, nil
}

func (logger *Logger) Start() error {
	return logger.withPtr("start", func(ptr *C.mp_logger_t) C.mp_log_status_t {
		return C.mp_logger_start(ptr)
	})
}

func (logger *Logger) Log(level Level, message string, context string) error {
	messageText := C.CString(message)
	defer C.free(unsafe.Pointer(messageText))

	var contextText *C.char
	if context != "" {
		contextText = C.CString(context)
		defer C.free(unsafe.Pointer(contextText))
	}

	return logger.withPtr("log", func(ptr *C.mp_logger_t) C.mp_log_status_t {
		return C.mp_logger_log(ptr, C.mp_log_level_t(level), messageText, contextText)
	})
}

func (logger *Logger) Flush(timeout time.Duration) error {
	timeoutMillis, err := durationToMillis(timeout)
	if err != nil {
		return err
	}

	return logger.withPtr("flush", func(ptr *C.mp_logger_t) C.mp_log_status_t {
		return C.mp_logger_flush(ptr, timeoutMillis)
	})
}

func (logger *Logger) Stats() (Stats, error) {
	logger.mu.RLock()
	defer logger.mu.RUnlock()
	if logger.ptr == nil {
		return Stats{}, ErrClosed
	}

	var stats C.mp_logger_stats_t
	status := C.mp_logger_get_stats(logger.ptr, &stats)
	if err := statusError("get_stats", status); err != nil {
		return Stats{}, err
	}
	return Stats{
		QueuedRecords:     uint64(stats.queued_records),
		ProcessedRecords:  uint64(stats.processed_records),
		DroppedBusy:       uint64(stats.dropped_busy),
		DroppedFull:       uint64(stats.dropped_full),
		ActiveStreamCount: int(stats.active_stream_count),
	}, nil
}

func (logger *Logger) Shutdown(timeout time.Duration) error {
	timeoutMillis, err := durationToMillis(timeout)
	if err != nil {
		return err
	}

	return logger.withPtr("shutdown", func(ptr *C.mp_logger_t) C.mp_log_status_t {
		return C.mp_logger_shutdown(ptr, timeoutMillis)
	})
}

func (logger *Logger) Close() {
	logger.mu.Lock()
	ptr := logger.ptr
	logger.ptr = nil
	logger.mu.Unlock()

	if ptr != nil {
		C.mp_logger_destroy(ptr)
	}
}

func (config Config) toC() (C.mp_logger_config_t, error) {
	var out C.mp_logger_config_t
	var err error

	C.mp_logger_config_init_defaults(&out)
	if out.buffer_capacity, err = toSize(config.BufferCapacity, "buffer_capacity"); err != nil {
		return C.mp_logger_config_t{}, err
	}
	if out.message_capacity, err = toSize(config.MessageCapacity, "message_capacity"); err != nil {
		return C.mp_logger_config_t{}, err
	}
	if out.context_capacity, err = toSize(config.ContextCapacity, "context_capacity"); err != nil {
		return C.mp_logger_config_t{}, err
	}
	out.format = C.mp_log_format_t(config.Format)
	if config.PrettyOutput {
		out.pretty_output = 1
	} else {
		out.pretty_output = 0
	}
	out.stdout_min_level = C.mp_log_level_t(config.StdoutMinLevel)
	out.stdout_max_level = C.mp_log_level_t(config.StdoutMaxLevel)
	out.stderr_min_level = C.mp_log_level_t(config.StderrMinLevel)
	out.stderr_max_level = C.mp_log_level_t(config.StderrMaxLevel)
	out.file_min_level = C.mp_log_level_t(config.FileMinLevel)
	out.file_max_level = C.mp_log_level_t(config.FileMaxLevel)
	out.udp_min_level = C.mp_log_level_t(config.UDPMinLevel)
	out.udp_max_level = C.mp_log_level_t(config.UDPMaxLevel)
	out.udp_port = C.uint16_t(config.UDPPort)

	if err := writeCString(&out.service_name[0], int(C.MP_LOGGER_NAME_CAPACITY), config.ServiceName, "service_name"); err != nil {
		return C.mp_logger_config_t{}, err
	}
	if err := writeCString(&out.environment_name[0], int(C.MP_LOGGER_NAME_CAPACITY), config.EnvironmentName, "environment_name"); err != nil {
		return C.mp_logger_config_t{}, err
	}
	if err := writeCString(&out.log_directory[0], int(C.MP_LOGGER_PATH_CAPACITY), config.LogDirectory, "log_directory"); err != nil {
		return C.mp_logger_config_t{}, err
	}
	if err := writeCString(&out.file_name_prefix[0], int(C.MP_LOGGER_NAME_CAPACITY), config.FileNamePrefix, "file_name_prefix"); err != nil {
		return C.mp_logger_config_t{}, err
	}
	if err := writeCString(&out.backup_file_name_prefix[0], int(C.MP_LOGGER_NAME_CAPACITY), config.BackupFileNamePrefix, "backup_file_name_prefix"); err != nil {
		return C.mp_logger_config_t{}, err
	}
	if err := writeCString(&out.active_streams[0], int(C.MP_LOGGER_ACTIVE_STREAMS_CAPACITY), config.ActiveStreams, "active_streams"); err != nil {
		return C.mp_logger_config_t{}, err
	}
	if err := writeCString(&out.udp_host[0], int(C.MP_LOGGER_HOST_CAPACITY), config.UDPHost, "udp_host"); err != nil {
		return C.mp_logger_config_t{}, err
	}

	return out, nil
}

func configFromC(config C.mp_logger_config_t) Config {
	return Config{
		ServiceName:          readCString(&config.service_name[0]),
		EnvironmentName:      readCString(&config.environment_name[0]),
		BufferCapacity:       int(config.buffer_capacity),
		MessageCapacity:      int(config.message_capacity),
		ContextCapacity:      int(config.context_capacity),
		Format:               Format(config.format),
		PrettyOutput:         config.pretty_output != 0,
		LogDirectory:         readCString(&config.log_directory[0]),
		FileNamePrefix:       readCString(&config.file_name_prefix[0]),
		BackupFileNamePrefix: readCString(&config.backup_file_name_prefix[0]),
		ActiveStreams:        readCString(&config.active_streams[0]),
		StdoutMinLevel:       Level(config.stdout_min_level),
		StdoutMaxLevel:       Level(config.stdout_max_level),
		StderrMinLevel:       Level(config.stderr_min_level),
		StderrMaxLevel:       Level(config.stderr_max_level),
		FileMinLevel:         Level(config.file_min_level),
		FileMaxLevel:         Level(config.file_max_level),
		UDPMinLevel:          Level(config.udp_min_level),
		UDPMaxLevel:          Level(config.udp_max_level),
		UDPHost:              readCString(&config.udp_host[0]),
		UDPPort:              uint16(config.udp_port),
	}
}

func statusError(op string, status C.mp_log_status_t) error {
	if status == C.MP_LOG_STATUS_OK {
		return nil
	}
	return &StatusError{
		Op:     op,
		Status: Status(status),
	}
}

func readCString(value *C.char) string {
	return C.GoString(value)
}

func writeCString(dst *C.char, capacity int, value string, field string) error {
	if capacity <= 0 {
		return fmt.Errorf("%s has invalid capacity", field)
	}
	if len(value) >= capacity {
		return fmt.Errorf("%s exceeds %d bytes", field, capacity-1)
	}
	buffer := unsafe.Slice((*byte)(unsafe.Pointer(dst)), capacity)
	clear(buffer)
	copy(buffer, value)
	return nil
}

func durationToMillis(timeout time.Duration) (C.uint32_t, error) {
	if timeout < 0 {
		return 0, fmt.Errorf("timeout must be non-negative")
	}
	timeoutMillis := timeout.Milliseconds()
	if timeoutMillis > maxTimeoutMillis {
		return 0, fmt.Errorf("timeout exceeds %dms", uint64(maxTimeoutMillis))
	}
	return C.uint32_t(timeoutMillis), nil
}

func toSize(value int, field string) (C.size_t, error) {
	if value < 0 {
		return 0, fmt.Errorf("%s must be non-negative", field)
	}
	return C.size_t(value), nil
}

func (logger *Logger) withPtr(op string, call func(*C.mp_logger_t) C.mp_log_status_t) error {
	logger.mu.RLock()
	defer logger.mu.RUnlock()
	if logger.ptr == nil {
		return ErrClosed
	}
	return statusError(op, call(logger.ptr))
}
