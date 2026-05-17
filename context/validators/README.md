# Validators

## Static

- command: `./scripts/validate-static.sh`
- proves: required files exist, scripts are executable, install/test hooks exist, the bundled Go wrapper is present, the config template exists, the local commit-message rule and hook are wired, public APIs and selected complex functions retain comments, structured field APIs are documented, durable values stay centralized in `include/mp_logger_constants.h`, unsafe C functions are absent, and the logger still uses non-blocking queue admission.

## Commit Message

- command: `./scripts/validate-commit-message.sh <path>`
- proves: a commit subject matches the local `<type>(<scope>): <summary>` rule, a blank separator line is present, and a non-empty body exists.

## LLM

- command: `./scripts/validate-llm.sh`
- proves: semantic review prompt exists for drop policy, sink isolation, log hygiene, and extension safety.

## Benchmarks

- command: `./scripts/benchmark.sh`
- proves: file-stream throughput, multi-stream fanout cost, queue memory footprint, deterministic fill point, paced overflow threshold, and concurrent producer behavior on the current host.
