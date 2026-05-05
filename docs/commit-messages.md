# Commit Message Standard

All commits in this repository must use a subject and a body. Do not create one-line commits.

## Required Format

Every commit message must follow this shape:

```text
<type>(<scope>): <summary>

<body>
```

Rules:

- The subject line is required and must be a single line.
- The body is required and must be separated from the subject by one blank line.
- The body must explain what changed and why it changed.
- The body may be prose or short flat bullets, but it must be meaningful and specific to the diff.
- The subject should stay concise and use the imperative mood.

## Subject Rules

Use a conventional subject in the form `<type>(<scope>): <summary>`.

Supported types:

- `feat`
- `fix`
- `docs`
- `test`
- `refactor`
- `build`
- `chore`

Scope rules:

- Use a short scope that matches the part of the repository being changed.
- Prefer existing scopes when they already fit, such as `logger-core` or `readme`.

Summary rules:

- Describe the change, not the ticket or intent alone.
- Keep it specific enough to distinguish the commit from nearby history.

## Body Rules

The body is mandatory for every commit in this repository.

The body must:

- summarize the main code or documentation changes;
- state the reason for the change, the effect of the change, or both;
- mention validation when it materially helps review, such as tests or benchmark runs.

The body must not:

- repeat the subject without adding new information;
- use placeholder text such as `misc updates`;
- omit rationale when the diff changes behavior, validation, or developer workflow.

## Examples

```text
feat(logger-core): add file stream benchmarks

Add a release-mode benchmark target for file sink throughput and
multi-stream fanout.

Document the measured host results in the README and extend static
validation so the benchmark workflow remains discoverable.
```

```text
docs(readme): add go usage examples

Document cgo-based Go integration examples for bootstrap and manual
configuration flows.

Keep the examples aligned with the public C API and clarify lifecycle
handling for create, log, flush, shutdown, and destroy.
```
