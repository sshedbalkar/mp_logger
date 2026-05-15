package mplogger

import (
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"testing"
)

const repoTempRootRelativePath = ".tmp/temp"

func repoTempDir(t *testing.T) string {
	t.Helper()

	root, err := repoTempRoot()
	if err != nil {
		t.Fatalf("repoTempRoot() error = %v", err)
	}
	if err := os.MkdirAll(root, 0o755); err != nil {
		t.Fatalf("MkdirAll(%q) error = %v", root, err)
	}

	dir, err := os.MkdirTemp(root, "mp-logger-go-test-")
	if err != nil {
		t.Fatalf("MkdirTemp(%q) error = %v", root, err)
	}
	t.Cleanup(func() {
		_ = os.RemoveAll(dir)
	})
	return dir
}

func repoTempRoot() (string, error) {
	_, file, _, ok := runtime.Caller(0)
	if !ok {
		return "", fmt.Errorf("resolve helper path")
	}
	return filepath.Clean(filepath.Join(filepath.Dir(file), "..", "..", "..", "..", repoTempRootRelativePath)), nil
}
