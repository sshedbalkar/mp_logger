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

	if config.ServiceName != "mp-service" {
		t.Fatalf("ServiceName = %q", config.ServiceName)
	}
	if config.EnvironmentName != "development" {
		t.Fatalf("EnvironmentName = %q", config.EnvironmentName)
	}
	if config.ActiveStreams != "stdout,stderr,file" {
		t.Fatalf("ActiveStreams = %q", config.ActiveStreams)
	}
	if config.Format != JSON {
		t.Fatalf("Format = %v", config.Format)
	}
	if config.UDPPort != 5514 {
		t.Fatalf("UDPPort = %d", config.UDPPort)
	}
}

func TestCreateStartLogFlushShutdown(t *testing.T) {
	tempDir := t.TempDir()
	config := DefaultConfig()
	config.ActiveStreams = "file"
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
	tempDir := t.TempDir()
	configPath := filepath.Join(tempDir, "logger.ini")
	configText := strings.Join([]string{
		"service_name = go-bootstrap",
		"environment_name = test",
		"",
		"[logger]",
		"buffer_capacity = 8",
		"message_capacity = 96",
		"context_capacity = 96",
		"format = text",
		"pretty_output = false",
		"log_directory = " + tempDir,
		"file_name_prefix = go-bootstrap",
		"backup_file_name_prefix = go-bootstrap-internal",
		"active_streams = file",
		"",
		"[file]",
		"minimum_level = trace",
		"maximum_level = fatal",
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
	config.ServiceName = strings.Repeat("x", 64)

	if _, err := Create(config); err == nil {
		t.Fatalf("expected Create() to reject long service name")
	}
}
