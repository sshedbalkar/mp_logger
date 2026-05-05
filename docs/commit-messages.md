# Commit Message Standard

All commits in this repository must use a subject and a body. Do not create one-line commits.

The body is mandatory for every commit in this repository.

## Required Format

Every commit message must follow this shape:

```text
<type>(<scope>): <short summary>                          ← required
<empty line>
<body — explains the WHY, not the WHAT, under 20 words>   ← required
<empty line>
<footer>                                                  ← situational
```

Types:

- `feat`
- `fix`
- `refactor`
- `test`
- `docs`
- `build`
- `perf`
- `security`
- `db`
- `ops`
- `style`
- `ci`
- `chore`
- `revert`
- `hotfix`
- `infra`


Footers:

- `Fixes <ID>`
- `Closes <ID>`
- `Refs <ID>`
- `BREAKING CHANGE:`
- `Co-authored-by`
- `Reviewed-by`


## Examples

```text
feat(logger-core): add file stream benchmarks

Add a release-mode benchmark target for file sink throughput and
multi-stream fanout.

Document the measured host results in the README and extend static
validation so the benchmark workflow remains discoverable.

Co-authored-by GPT 5.4
```

```text
docs(readme): add go usage examples

Document cgo-based Go integration examples for bootstrap and manual
configuration flows.

Keep the examples aligned with the public C API and clarify lifecycle
handling for create, log, flush, shutdown, and destroy.

Fixes Issue#123
```
