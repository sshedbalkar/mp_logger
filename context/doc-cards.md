# Doc Cards

## Entries

- `readme`: `README.md` -- module overview and commands
- `contract.constants`: `include/mp_logger_constants.h` -- central source of truth for durable defaults, config keys, status names, stream names, file markers, capacities, and date formats
- `config.script-defaults`: `configs/scripts/defaults.env` -- centralized shell-script defaults for build, test, deploy, benchmark, report, hook, and temp settings
- `binding.go`: `bindings/go/README.md` -- bundled cgo wrapper usage and limits
- `doc.standards`: `docs/standards.md` -- extracted rules for logger work
- `doc.architecture`: `docs/architecture.md` -- runtime and extension design
- `doc.commit-messages`: `docs/commit-messages.md` -- local commit subject and mandatory body rules
- `validator.commit-message`: `scripts/validate-commit-message.sh` -- commit subject/body validator used by the repo hook
- `benchmark.runner`: `scripts/benchmark.sh` -- release-mode benchmark runner and report generator for file throughput, saturation, and concurrency
- `validator.static`: `scripts/validate-static.sh` -- deterministic validator
- `validator.llm`: `scripts/validate-llm.sh` -- semantic review prompt generator
