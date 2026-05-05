# mp_logger Standards

## Scope

These rules are the logger-local extraction of the host project's native C, observability, testing, security, dependency, and build standards.

## Design Rules

- Keep the public C API small, explicit, and documented.
- Keep logging transport concerns separate from application business logic.
- Keep log calls non-blocking with bounded buffering and explicit drop behavior.
- Keep memory ownership local to the logger and its registered stream implementations.
- Keep stream additions pluggable through callbacks instead of hard-coding app-specific behavior.

## Configuration Rules

- Bootstrap config must be explicit and file-backed.
- Default file output must write to the application root unless config overrides it.
- Every process run must create a new primary log file and a new internal backup log file.
- Config parsing must reject malformed keys or values rather than guessing.

## Security And Privacy Rules

- Never write secrets or uncontrolled raw binary into logs.
- Escape control characters so log lines cannot forge extra entries.
- Keep network sinks optional and config-driven.
- Route logger-internal warnings and failures to the backup file logger, not back into the primary queue.

## Observability Rules

- Every record must include timestamp, level, service, environment, sequence, and message.
- Optional context text must remain separate from the primary message.
- High-volume sink I/O must happen on the worker thread, never on the producer path.
- Queue drops caused by contention or saturation must be visible through stats and backup log warnings.

## Validation Rules

- Unit tests must cover bootstrap parsing, queue pressure, custom stream behavior, and file output.
- Static validation must check API presence, test presence, install rules, config templates, and unsafe C function absence.
- LLM review is reserved for semantic checks such as drop policy suitability, log hygiene, and extension safety.
