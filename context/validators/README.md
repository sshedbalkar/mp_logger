# Validators

## Static

- command: `./scripts/validate-static.sh`
- proves: required files exist, scripts are executable, install/test hooks exist, the config template exists, unsafe C functions are absent, and the logger still uses non-blocking queue admission.

## LLM

- command: `./scripts/validate-llm.sh`
- proves: semantic review prompt exists for drop policy, sink isolation, log hygiene, and extension safety.

## Benchmarks

- command: `./scripts/benchmark.sh`
- proves: file-stream throughput, multi-stream fanout cost, queue memory footprint, deterministic fill point, paced overflow threshold, and concurrent producer behavior on the current host.
