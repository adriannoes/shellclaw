# Agent quickstart

ShellClaw is a C99 AI agent for edge hardware (Jetson / Raspberry Pi). Read this before editing code.

## Commands

| Action | Command |
|--------|---------|
| Build | `make shellclaw` → `build/shellclaw` |
| Run all tests | `make test` |
| Static analysis | `make static` (requires cppcheck) |
| Coverage (core ≥ 80%) | `make coverage` (requires lcov) |
| Full CI locally | `chmod +x scripts/ci-local.sh && ./scripts/ci-local.sh` (Ubuntu 24.04+; on Mac use `CI=true make test`) |
| Pre-PR check | `CI=true make clean && CI=true make test` |

Gateway tests need libwebsockets: `GATEWAY=1 make test_gateway_http`.

## Layout

```
src/
  core/       agent loop, config, memory, skills
  providers/  LLM providers (Anthropic, OpenAI, local)
  tools/      shell, file, web_search, asap_invoke, cron, context
  channels/   CLI, Telegram, Discord, WebChat
  gateway/    HTTP server, WebSocket, embedded Web UI
  asap/       ASAP Protocol client/server, registry, manifest
  sandbox/    Linux namespaces + cgroups v2
  hardware/   GPIO/I2C/camera abstraction (per-board backends)
  crypto/     Ed25519 signing
tests/        one test binary per module (Makefile targets)
```

## Conventions

- Language: C99/C11, English comments, Doxygen on public APIs.
- Types: explicit on every variable, parameter, and return value.
- Config/secrets: environment variables and `.env.example` — never commit credentials.
- Tests must run headless with no manual setup or secrets.
- Bug fix: write a failing regression test first, then fix.

## Branches

- `main` — stable release line.
- `development` — integration branch for Phase 5+ work; open PRs here first.
- Phase slices land as focused feature branches merged into `development`; merge `development` → `main` when hardware validation on target boards is done.

## Rules

Project rules live in `.cursor/rules/`:

- `c-principles.mdc` — C coding standards
- `agent-clean-code-c.mdc` — agent-oriented clean code for C
- `shellclaw-architecture.mdc` — product architecture
- `testing.mdc` — test workflow and CI
- `git-commits.mdc` — Conventional Commits
- `karpathy-guidelines.mdc` — behavioral guidelines for agents
