package mplogger

/*
#cgo CFLAGS: -std=c17 -Wall -Wextra -Wpedantic -I${SRCDIR}/../../include -I${SRCDIR}/../../src
#cgo LDFLAGS: -lpthread
#include <stdlib.h>
#include "mp_logger.h"

static inline void go_mp_logger_set_field_string(mp_log_field_t *field, const char *key, const char *value) {
	*field = mp_log_field_string(key, value);
}

static inline void go_mp_logger_set_field_bool(mp_log_field_t *field, const char *key, _Bool value) {
	*field = mp_log_field_bool(key, value);
}

static inline void go_mp_logger_set_field_int64(mp_log_field_t *field, const char *key, int64_t value) {
	*field = mp_log_field_int64(key, value);
}

static inline void go_mp_logger_set_field_uint64(mp_log_field_t *field, const char *key, uint64_t value) {
	*field = mp_log_field_uint64(key, value);
}

static inline void go_mp_logger_set_field_float64(mp_log_field_t *field, const char *key, double value) {
	*field = mp_log_field_float64(key, value);
}

static inline const char *go_mp_logger_default_service_name(void) { return MP_LOGGER_DEFAULT_SERVICE_NAME; }
static inline const char *go_mp_logger_default_environment_name(void) { return MP_LOGGER_DEFAULT_ENVIRONMENT_NAME; }
static inline const char *go_mp_logger_default_active_streams(void) { return MP_LOGGER_DEFAULT_ACTIVE_STREAMS; }
static inline const char *go_mp_logger_config_key_service_name(void) { return MP_LOGGER_CONFIG_KEY_SERVICE_NAME; }
static inline const char *go_mp_logger_config_key_environment_name(void) { return MP_LOGGER_CONFIG_KEY_ENVIRONMENT_NAME; }
static inline const char *go_mp_logger_config_key_buffer_capacity(void) { return MP_LOGGER_CONFIG_KEY_BUFFER_CAPACITY; }
static inline const char *go_mp_logger_config_key_message_capacity(void) { return MP_LOGGER_CONFIG_KEY_MESSAGE_CAPACITY; }
static inline const char *go_mp_logger_config_key_context_capacity(void) { return MP_LOGGER_CONFIG_KEY_CONTEXT_CAPACITY; }
static inline const char *go_mp_logger_config_key_field_capacity(void) { return MP_LOGGER_CONFIG_KEY_FIELD_CAPACITY; }
static inline const char *go_mp_logger_config_key_field_key_capacity(void) { return MP_LOGGER_CONFIG_KEY_FIELD_KEY_CAPACITY; }
static inline const char *go_mp_logger_config_key_field_value_capacity(void) { return MP_LOGGER_CONFIG_KEY_FIELD_VALUE_CAPACITY; }
static inline const char *go_mp_logger_config_key_format(void) { return MP_LOGGER_CONFIG_KEY_FORMAT; }
static inline const char *go_mp_logger_config_key_pretty_output(void) { return MP_LOGGER_CONFIG_KEY_PRETTY_OUTPUT; }
static inline const char *go_mp_logger_config_key_log_directory(void) { return MP_LOGGER_CONFIG_KEY_LOG_DIRECTORY; }
static inline const char *go_mp_logger_config_key_file_name_prefix(void) { return MP_LOGGER_CONFIG_KEY_FILE_NAME_PREFIX; }
static inline const char *go_mp_logger_config_key_backup_file_name_prefix(void) { return MP_LOGGER_CONFIG_KEY_BACKUP_FILE_NAME_PREFIX; }
static inline const char *go_mp_logger_config_key_active_streams(void) { return MP_LOGGER_CONFIG_KEY_ACTIVE_STREAMS; }
static inline const char *go_mp_logger_config_key_minimum_level(void) { return MP_LOGGER_CONFIG_KEY_MINIMUM_LEVEL; }
static inline const char *go_mp_logger_config_key_maximum_level(void) { return MP_LOGGER_CONFIG_KEY_MAXIMUM_LEVEL; }
static inline const char *go_mp_logger_config_key_host(void) { return MP_LOGGER_CONFIG_KEY_HOST; }
static inline const char *go_mp_logger_config_key_port(void) { return MP_LOGGER_CONFIG_KEY_PORT; }
static inline const char *go_mp_logger_config_section_logger(void) { return MP_LOGGER_CONFIG_SECTION_LOGGER; }
static inline const char *go_mp_logger_config_section_file(void) { return MP_LOGGER_CONFIG_SECTION_FILE; }
static inline const char *go_mp_logger_stream_file(void) { return MP_LOGGER_STREAM_FILE; }
static inline const char *go_mp_logger_format_name_text(void) { return MP_LOGGER_FORMAT_NAME_TEXT; }
static inline const char *go_mp_logger_bool_false(void) { return MP_LOGGER_BOOL_FALSE; }
static inline const char *go_mp_logger_level_token_trace(void) { return MP_LOGGER_LEVEL_TOKEN_TRACE; }
static inline const char *go_mp_logger_level_token_fatal(void) { return MP_LOGGER_LEVEL_TOKEN_FATAL; }
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

// Centralized contract values mirrored from the C header for wrapper code and tests.
var (
	defaultServiceName      = cString(C.go_mp_logger_default_service_name())
	defaultEnvironmentName  = cString(C.go_mp_logger_default_environment_name())
	defaultActiveStreams    = cString(C.go_mp_logger_default_active_streams())
	defaultFieldCapacity    = int(C.MP_LOGGER_DEFAULT_FIELD_CAPACITY)
	defaultUDPPort          = uint16(C.MP_LOGGER_DEFAULT_UDP_PORT)
	nameCapacity            = int(C.MP_LOGGER_NAME_CAPACITY)
	configKeyServiceName    = cString(C.go_mp_logger_config_key_service_name())
	configKeyEnvironment    = cString(C.go_mp_logger_config_key_environment_name())
	configKeyBufferCapacity = cString(C.go_mp_logger_config_key_buffer_capacity())
	configKeyMessageCap     = cString(C.go_mp_logger_config_key_message_capacity())
	configKeyContextCap     = cString(C.go_mp_logger_config_key_context_capacity())
	configKeyFieldCap       = cString(C.go_mp_logger_config_key_field_capacity())
	configKeyFieldKeyCap    = cString(C.go_mp_logger_config_key_field_key_capacity())
	configKeyFieldValueCap  = cString(C.go_mp_logger_config_key_field_value_capacity())
	configKeyFormat         = cString(C.go_mp_logger_config_key_format())
	configKeyPrettyOutput   = cString(C.go_mp_logger_config_key_pretty_output())
	configKeyLogDirectory   = cString(C.go_mp_logger_config_key_log_directory())
	configKeyFilePrefix     = cString(C.go_mp_logger_config_key_file_name_prefix())
	configKeyBackupPrefix   = cString(C.go_mp_logger_config_key_backup_file_name_prefix())
	configKeyActiveStreams  = cString(C.go_mp_logger_config_key_active_streams())
	configKeyMinimumLevel   = cString(C.go_mp_logger_config_key_minimum_level())
	configKeyMaximumLevel   = cString(C.go_mp_logger_config_key_maximum_level())
	configKeyHost           = cString(C.go_mp_logger_config_key_host())
	configKeyPort           = cString(C.go_mp_logger_config_key_port())
	configSectionLogger     = cString(C.go_mp_logger_config_section_logger())
	configSectionFile       = cString(C.go_mp_logger_config_section_file())
	streamFile              = cString(C.go_mp_logger_stream_file())
	formatNameText          = cString(C.go_mp_logger_format_name_text())
	boolFalse               = cString(C.go_mp_logger_bool_false())
	levelTokenTrace         = cString(C.go_mp_logger_level_token_trace())
	levelTokenFatal         = cString(C.go_mp_logger_level_token_fatal())
)

// Level mirrors the C log severity enum used by queue admission and sink filtering.
type Level int

const (
	Trace   Level = Level(C.MP_LOG_LEVEL_TRACE)
	Debug   Level = Level(C.MP_LOG_LEVEL_DEBUG)
	Info    Level = Level(C.MP_LOG_LEVEL_INFO)
	Warning Level = Level(C.MP_LOG_LEVEL_WARNING)
	Error   Level = Level(C.MP_LOG_LEVEL_ERROR)
	Fatal   Level = Level(C.MP_LOG_LEVEL_FATAL)
)

// String returns the stable uppercase name used by the C logger for this level.
func (level Level) String() string {
	return C.GoString(C.mp_log_level_name(C.mp_log_level_t(level)))
}

// Format selects the rendered wire format emitted to every configured sink.
type Format int

const (
	Text Format = Format(C.MP_LOG_FORMAT_TEXT)
	JSON Format = Format(C.MP_LOG_FORMAT_JSON)
)

// Status mirrors the C status codes returned by lifecycle and enqueue operations.
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

// String returns the stable uppercase name used by the C logger for this status.
func (status Status) String() string {
	return C.GoString(C.mp_log_status_name(C.mp_log_status_t(status)))
}

// StatusError wraps a non-OK C status with the operation that produced it.
type StatusError struct {
	Op     string
	Status Status
}

// Error renders the operation name and logger status in a human-readable form.
func (err *StatusError) Error() string {
	if err == nil {
		return "<nil>"
	}
	if err.Op == "" {
		return err.Status.String()
	}
	return fmt.Sprintf("%s: %s", err.Op, err.Status)
}

// ErrClosed reports that a method was called after Close released the C logger.
var ErrClosed = errors.New("mp_logger: logger closed")

// Config mirrors mp_logger_config_t so Go callers can build or load logger settings.
type Config struct {
	ServiceName          string
	EnvironmentName      string
	BufferCapacity       int
	MessageCapacity      int
	ContextCapacity      int
	FieldCapacity        int
	FieldKeyCapacity     int
	FieldValueCapacity   int
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

// Stats mirrors mp_logger_stats_t so Go callers can observe queue pressure and drops.
type Stats struct {
	QueuedRecords     uint64
	ProcessedRecords  uint64
	DroppedBusy       uint64
	DroppedFull       uint64
	ActiveStreamCount int
}

// fieldKind tracks which union member of Field is populated before cgo conversion.
type fieldKind uint8

const (
	fieldKindString fieldKind = iota
	fieldKindBool
	fieldKindInt64
	fieldKindUint64
	fieldKindFloat64
)

// Field carries one typed structured attribute for LogFields.
type Field struct {
	Key          string
	kind         fieldKind
	stringValue  string
	boolValue    bool
	int64Value   int64
	uint64Value  uint64
	float64Value float64
}

// Logger owns the underlying C logger pointer and serializes access to Close.
type Logger struct {
	mu  sync.RWMutex
	ptr *C.mp_logger_t
}

// DefaultConfig returns the library defaults from mp_logger_config_init_defaults().
func DefaultConfig() Config {
	var config C.mp_logger_config_t
	C.mp_logger_config_init_defaults(&config)
	return configFromC(config)
}

// LoadBootstrapConfig parses a bootstrap INI file into a Config without creating a logger.
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

// Create allocates a logger from config without starting the worker thread.
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

// CreateFromBootstrap loads a bootstrap file, creates the logger, and starts the worker.
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

// String returns a string-valued structured field for LogFields.
func String(key string, value string) Field {
	return Field{Key: key, kind: fieldKindString, stringValue: value}
}

// Bool returns a boolean structured field for LogFields.
func Bool(key string, value bool) Field {
	return Field{Key: key, kind: fieldKindBool, boolValue: value}
}

// Int64 returns a signed 64-bit structured field for LogFields.
func Int64(key string, value int64) Field {
	return Field{Key: key, kind: fieldKindInt64, int64Value: value}
}

// Uint64 returns an unsigned 64-bit structured field for LogFields.
func Uint64(key string, value uint64) Field {
	return Field{Key: key, kind: fieldKindUint64, uint64Value: value}
}

// Float64 returns a float64 structured field for LogFields.
func Float64(key string, value float64) Field {
	return Field{Key: key, kind: fieldKindFloat64, float64Value: value}
}

// Start launches the worker thread that drains queued records to sinks.
func (logger *Logger) Start() error {
	return logger.withPtr("start", func(ptr *C.mp_logger_t) C.mp_log_status_t {
		return C.mp_logger_start(ptr)
	})
}

// Log attempts to enqueue one record without blocking on sink I/O.
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

// LogFields attempts to enqueue one record plus typed structured fields without blocking on sink I/O.
func (logger *Logger) LogFields(level Level, message string, context string, fields ...Field) error {
	messageText := C.CString(message)
	defer C.free(unsafe.Pointer(messageText))

	var contextText *C.char
	if context != "" {
		contextText = C.CString(context)
		defer C.free(unsafe.Pointer(contextText))
	}

	cFields, allocations, err := fieldsToC(fields)
	if err != nil {
		return err
	}
	defer freeAllocations(allocations)
	if cFields != nil {
		defer C.free(unsafe.Pointer(cFields))
	}

	return logger.withPtr("log_fields", func(ptr *C.mp_logger_t) C.mp_log_status_t {
		return C.mp_logger_log_fields(
			ptr,
			C.mp_log_level_t(level),
			messageText,
			contextText,
			cFields,
			C.size_t(len(fields)),
		)
	})
}

// Flush waits until records queued before the call are processed or the timeout expires.
func (logger *Logger) Flush(timeout time.Duration) error {
	timeoutMillis, err := durationToMillis(timeout)
	if err != nil {
		return err
	}

	return logger.withPtr("flush", func(ptr *C.mp_logger_t) C.mp_log_status_t {
		return C.mp_logger_flush(ptr, timeoutMillis)
	})
}

// Stats returns a snapshot of cumulative queue and drop counters.
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

// Shutdown asks the worker to drain queued records and stop before the timeout expires.
func (logger *Logger) Shutdown(timeout time.Duration) error {
	timeoutMillis, err := durationToMillis(timeout)
	if err != nil {
		return err
	}

	return logger.withPtr("shutdown", func(ptr *C.mp_logger_t) C.mp_log_status_t {
		return C.mp_logger_shutdown(ptr, timeoutMillis)
	})
}

// Close destroys the underlying C logger and makes future method calls return ErrClosed.
func (logger *Logger) Close() {
	logger.mu.Lock()
	ptr := logger.ptr
	logger.ptr = nil
	logger.mu.Unlock()

	if ptr != nil {
		C.mp_logger_destroy(ptr)
	}
}

// toC validates Config and copies it into the C struct layout expected by mp_logger_create.
func (config Config) toC() (C.mp_logger_config_t, error) {
	var out C.mp_logger_config_t
	var err error

	C.mp_logger_config_init_defaults(&out)
	if out.buffer_capacity, err = toSize(config.BufferCapacity, configKeyBufferCapacity); err != nil {
		return C.mp_logger_config_t{}, err
	}
	if out.message_capacity, err = toSize(config.MessageCapacity, configKeyMessageCap); err != nil {
		return C.mp_logger_config_t{}, err
	}
	if out.context_capacity, err = toSize(config.ContextCapacity, configKeyContextCap); err != nil {
		return C.mp_logger_config_t{}, err
	}
	if out.field_capacity, err = toSize(config.FieldCapacity, configKeyFieldCap); err != nil {
		return C.mp_logger_config_t{}, err
	}
	if out.field_key_capacity, err = toSize(config.FieldKeyCapacity, configKeyFieldKeyCap); err != nil {
		return C.mp_logger_config_t{}, err
	}
	if out.field_value_capacity, err = toSize(config.FieldValueCapacity, configKeyFieldValueCap); err != nil {
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

	if err := writeCString(&out.service_name[0], int(C.MP_LOGGER_NAME_CAPACITY), config.ServiceName, configKeyServiceName); err != nil {
		return C.mp_logger_config_t{}, err
	}
	if err := writeCString(&out.environment_name[0], int(C.MP_LOGGER_NAME_CAPACITY), config.EnvironmentName, configKeyEnvironment); err != nil {
		return C.mp_logger_config_t{}, err
	}
	if err := writeCString(&out.log_directory[0], int(C.MP_LOGGER_PATH_CAPACITY), config.LogDirectory, configKeyLogDirectory); err != nil {
		return C.mp_logger_config_t{}, err
	}
	if err := writeCString(&out.file_name_prefix[0], int(C.MP_LOGGER_NAME_CAPACITY), config.FileNamePrefix, configKeyFilePrefix); err != nil {
		return C.mp_logger_config_t{}, err
	}
	if err := writeCString(&out.backup_file_name_prefix[0], int(C.MP_LOGGER_NAME_CAPACITY), config.BackupFileNamePrefix, configKeyBackupPrefix); err != nil {
		return C.mp_logger_config_t{}, err
	}
	if err := writeCString(&out.active_streams[0], int(C.MP_LOGGER_ACTIVE_STREAMS_CAPACITY), config.ActiveStreams, configKeyActiveStreams); err != nil {
		return C.mp_logger_config_t{}, err
	}
	if err := writeCString(&out.udp_host[0], int(C.MP_LOGGER_HOST_CAPACITY), config.UDPHost, configKeyHost); err != nil {
		return C.mp_logger_config_t{}, err
	}

	return out, nil
}

// configFromC converts a C config snapshot into the Go wrapper type.
func configFromC(config C.mp_logger_config_t) Config {
	return Config{
		ServiceName:          readCString(&config.service_name[0]),
		EnvironmentName:      readCString(&config.environment_name[0]),
		BufferCapacity:       int(config.buffer_capacity),
		MessageCapacity:      int(config.message_capacity),
		ContextCapacity:      int(config.context_capacity),
		FieldCapacity:        int(config.field_capacity),
		FieldKeyCapacity:     int(config.field_key_capacity),
		FieldValueCapacity:   int(config.field_value_capacity),
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

// statusError maps a non-OK C status into the Go error surface.
func statusError(op string, status C.mp_log_status_t) error {
	if status == C.MP_LOG_STATUS_OK {
		return nil
	}
	return &StatusError{
		Op:     op,
		Status: Status(status),
	}
}

// fieldsToC materializes the temporary C allocations needed for one LogFields call.
func fieldsToC(fields []Field) (*C.mp_log_field_t, []unsafe.Pointer, error) {
	if len(fields) == 0 {
		return nil, nil, nil
	}

	size := C.size_t(len(fields)) * C.size_t(C.sizeof_mp_log_field_t)
	base := (*C.mp_log_field_t)(C.malloc(size))
	if base == nil {
		return nil, nil, fmt.Errorf("failed to allocate structured fields")
	}

	allocations := make([]unsafe.Pointer, 0, len(fields)*2)
	slice := unsafe.Slice(base, len(fields))
	for index, field := range fields {
		key := C.CString(field.Key)
		allocations = append(allocations, unsafe.Pointer(key))

		switch field.kind {
		case fieldKindString:
			value := C.CString(field.stringValue)
			allocations = append(allocations, unsafe.Pointer(value))
			C.go_mp_logger_set_field_string(&slice[index], key, value)
		case fieldKindBool:
			boolValue := C.bool(false)
			if field.boolValue {
				boolValue = C.bool(true)
			}
			C.go_mp_logger_set_field_bool(&slice[index], key, boolValue)
		case fieldKindInt64:
			C.go_mp_logger_set_field_int64(&slice[index], key, C.int64_t(field.int64Value))
		case fieldKindUint64:
			C.go_mp_logger_set_field_uint64(&slice[index], key, C.uint64_t(field.uint64Value))
		case fieldKindFloat64:
			C.go_mp_logger_set_field_float64(&slice[index], key, C.double(field.float64Value))
		default:
			C.free(unsafe.Pointer(base))
			freeAllocations(allocations)
			return nil, nil, fmt.Errorf("unsupported field kind for key %q", field.Key)
		}
	}

	return base, allocations, nil
}

// freeAllocations releases the temporary C strings created for a LogFields call.
func freeAllocations(allocations []unsafe.Pointer) {
	for _, allocation := range allocations {
		C.free(allocation)
	}
}

// readCString copies a NUL-terminated C string into Go.
func readCString(value *C.char) string {
	return C.GoString(value)
}

// cString copies a centralized C contract string into Go package scope.
func cString(value *C.char) string {
	return C.GoString(value)
}

// writeCString copies one Go string into a fixed-capacity C char array.
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

// durationToMillis converts a Go duration into the uint32 millisecond range used by the C API.
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

// toSize rejects negative Go ints before they are cast to C.size_t.
func toSize(value int, field string) (C.size_t, error) {
	if value < 0 {
		return 0, fmt.Errorf("%s must be non-negative", field)
	}
	return C.size_t(value), nil
}

// withPtr guards logger pointer access and maps the resulting C status into an error.
func (logger *Logger) withPtr(op string, call func(*C.mp_logger_t) C.mp_log_status_t) error {
	logger.mu.RLock()
	defer logger.mu.RUnlock()
	if logger.ptr == nil {
		return ErrClosed
	}
	return statusError(op, call(logger.ptr))
}
