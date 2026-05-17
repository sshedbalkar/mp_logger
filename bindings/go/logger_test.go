package mplogger

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestDefaultConfigMatchesCLibraryDefaults(t *testing.T) {
	config := DefaultConfig()

	if config.ServiceName != defaultServiceName {
		t.Fatalf("ServiceName = %q", config.ServiceName)
	}
	if config.EnvironmentName != defaultEnvironmentName {
		t.Fatalf("EnvironmentName = %q", config.EnvironmentName)
	}
	if config.ActiveStreams != defaultActiveStreams {
		t.Fatalf("ActiveStreams = %q", config.ActiveStreams)
	}
	if config.Format != JSON {
		t.Fatalf("Format = %v", config.Format)
	}
	if config.FieldCapacity != defaultFieldCapacity {
		t.Fatalf("FieldCapacity = %d", config.FieldCapacity)
	}
	if config.UDPPort != defaultUDPPort {
		t.Fatalf("UDPPort = %d", config.UDPPort)
	}
}

func TestCreateStartLogFlushShutdown(t *testing.T) {
	tempDir := repoTempDir(t)
	config := DefaultConfig()
	config.ActiveStreams = streamFile
	config.LogDirectory = tempDir
	config.FileNamePrefix = "go-wrapper"
	config.BackupFileNamePrefix = "wrapper-backup"

	logger, err := Create(config)
	if err != nil {
		t.Fatalf("Create() error = %v", err)
	}
	t.Cleanup(logger.Close)

	if err := logger.Start(); err != nil {
		t.Fatalf("Start() error = %v", err)
	}
	if err := logger.Log(Info, "service started", "request_id=123"); err != nil {
		t.Fatalf("Log() error = %v", err)
	}
	if err := logger.Flush(2 * time.Second); err != nil {
		t.Fatalf("Flush() error = %v", err)
	}

	stats, err := logger.Stats()
	if err != nil {
		t.Fatalf("Stats() error = %v", err)
	}
	if stats.ProcessedRecords != 1 {
		t.Fatalf("ProcessedRecords = %d", stats.ProcessedRecords)
	}
	if stats.ActiveStreamCount != 1 {
		t.Fatalf("ActiveStreamCount = %d", stats.ActiveStreamCount)
	}

	if err := logger.Shutdown(2 * time.Second); err != nil {
		t.Fatalf("Shutdown() error = %v", err)
	}

	matches, err := filepath.Glob(filepath.Join(tempDir, "go-wrapper*.log"))
	if err != nil {
		t.Fatalf("Glob() error = %v", err)
	}
	if len(matches) != 1 {
		t.Fatalf("expected 1 log file, got %d", len(matches))
	}

	logBytes, err := os.ReadFile(matches[0])
	if err != nil {
		t.Fatalf("ReadFile() error = %v", err)
	}
	logText := string(logBytes)
	if !strings.Contains(logText, "service started") {
		t.Fatalf("log file missing message: %q", logText)
	}
	if !strings.Contains(logText, "request_id=123") {
		t.Fatalf("log file missing context: %q", logText)
	}
}

func TestLoadBootstrapConfigAndCreateFromBootstrap(t *testing.T) {
	tempDir := repoTempDir(t)
	configPath := filepath.Join(tempDir, "logger.ini")
	configText := strings.Join([]string{
		configKeyServiceName + " = go-bootstrap",
		configKeyEnvironment + " = test",
		"",
		"[" + configSectionLogger + "]",
		configKeyBufferCapacity + " = 8",
		configKeyMessageCap + " = 96",
		configKeyContextCap + " = 96",
		configKeyFieldCap + " = 6",
		configKeyFieldKeyCap + " = 48",
		configKeyFieldValueCap + " = 96",
		configKeyFormat + " = " + formatNameText,
		configKeyPrettyOutput + " = " + boolFalse,
		configKeyLogDirectory + " = " + tempDir,
		configKeyFilePrefix + " = go-bootstrap",
		configKeyBackupPrefix + " = go-bootstrap-internal",
		configKeyActiveStreams + " = " + streamFile,
		"",
		"[" + configSectionFile + "]",
		configKeyMinimumLevel + " = " + levelTokenTrace,
		configKeyMaximumLevel + " = " + levelTokenFatal,
	}, "\n")

	if err := os.WriteFile(configPath, []byte(configText), 0o644); err != nil {
		t.Fatalf("WriteFile() error = %v", err)
	}

	config, err := LoadBootstrapConfig(configPath)
	if err != nil {
		t.Fatalf("LoadBootstrapConfig() error = %v", err)
	}
	if config.ServiceName != "go-bootstrap" {
		t.Fatalf("ServiceName = %q", config.ServiceName)
	}
	if config.FileNamePrefix != "go-bootstrap" {
		t.Fatalf("FileNamePrefix = %q", config.FileNamePrefix)
	}
	if config.FieldCapacity != 6 {
		t.Fatalf("FieldCapacity = %d", config.FieldCapacity)
	}

	logger, err := CreateFromBootstrap(configPath)
	if err != nil {
		t.Fatalf("CreateFromBootstrap() error = %v", err)
	}
	t.Cleanup(logger.Close)

	if err := logger.Log(Warning, "bootstrap ready", "mode=file"); err != nil {
		t.Fatalf("Log() error = %v", err)
	}
	if err := logger.Flush(2 * time.Second); err != nil {
		t.Fatalf("Flush() error = %v", err)
	}
	if err := logger.Shutdown(2 * time.Second); err != nil {
		t.Fatalf("Shutdown() error = %v", err)
	}

	matches, err := filepath.Glob(filepath.Join(tempDir, "go-bootstrap*.log"))
	if err != nil {
		t.Fatalf("Glob() error = %v", err)
	}
	if len(matches) == 0 {
		t.Fatalf("expected bootstrap log file")
	}
}

func TestCreateRejectsTooLongNames(t *testing.T) {
	config := DefaultConfig()
	config.ServiceName = strings.Repeat("x", nameCapacity)

	if _, err := Create(config); err == nil {
		t.Fatalf("expected Create() to reject long service name")
	}
}

func TestLogFieldsRendersStructuredValues(t *testing.T) {
	tempDir := repoTempDir(t)
	config := DefaultConfig()
	config.ActiveStreams = streamFile
	config.LogDirectory = tempDir
	config.FileNamePrefix = "go-structured"
	config.BackupFileNamePrefix = "go-internal"

	logger, err := Create(config)
	if err != nil {
		t.Fatalf("Create() error = %v", err)
	}
	t.Cleanup(logger.Close)

	if err := logger.Start(); err != nil {
		t.Fatalf("Start() error = %v", err)
	}
	if err := logger.LogFields(
		Info,
		"service started",
		"request_id=req-1",
		String("tenant", "alpha"),
		Bool("ok", true),
		Int64("attempt", 2),
		Uint64("bytes", 42),
		Float64("latency_ms", 12.5),
	); err != nil {
		t.Fatalf("LogFields() error = %v", err)
	}
	if err := logger.Flush(2 * time.Second); err != nil {
		t.Fatalf("Flush() error = %v", err)
	}
	if err := logger.Shutdown(2 * time.Second); err != nil {
		t.Fatalf("Shutdown() error = %v", err)
	}

	matches, err := filepath.Glob(filepath.Join(tempDir, "go-structured*.log"))
	if err != nil {
		t.Fatalf("Glob() error = %v", err)
	}
	if len(matches) != 1 {
		t.Fatalf("expected 1 log file, got %d", len(matches))
	}

	logBytes, err := os.ReadFile(matches[0])
	if err != nil {
		t.Fatalf("ReadFile() error = %v", err)
	}
	logText := string(logBytes)
	for _, needle := range []string{
		`"tenant":"alpha"`,
		`"ok":true`,
		`"attempt":2`,
		`"bytes":42`,
		`"latency_ms":12.5`,
	} {
		if !strings.Contains(logText, needle) {
			t.Fatalf("log file missing %s: %q", needle, logText)
		}
	}
}
