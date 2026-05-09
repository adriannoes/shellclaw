# ShellClaw

**The first physical AI agent that participates in a global agent ecosystem.**

A lightweight AI assistant written in C, designed to run on a Raspberry Pi Zero 2 W and communicate with other agents through the [agentic marketplace](https://asap-protocol.vercel.app/), using [ASAP Protocol](https://github.com/adriannoes/asap-protocol).

**Roadmap (high level):**

| Phase | Version | Status | Focus |
|-------|---------|--------|-------|
| 1: Foundation | v0.1.0 | ✅ Done | Core agent loop, CLI + Telegram, Anthropic/OpenAI, shell/search/file tools, SQLite memory & sessions, skill loading |
| 2: Gateway | v0.2.0 | ✅ Done | HTTP server, embedded Web UI, WebSocket chat, cron scheduler, pairing auth, ASAP manifest, skill hot-reload |
| 3: Protocol | v0.3.0 | ✅ Done | ASAP client/server, registry, `asap_invoke` tool, process sandbox (namespaces + cgroups), Tavily search, `/asap` + `/api/asap/log`, rate limits |
| 4: Autonomy | v0.4.0 | — | Local inference (llama.cpp), provider fallback, Discord channel, systemd service, OTA updates |
| 5: Hardware & Release | v1.0.0 | — | GPIO, I2C sensors, camera, Ed25519 signing, ASAP marketplace registration, security audit, full docs |

## What makes ShellClaw different

ShellClaw is **not another OpenClaw clone** in a different language. It is a **hardware-native agent** that interacts with the physical world (GPIO, I2C sensors, camera) while collaborating with cloud-based agents through a standardized protocol.

| Feature | ShellClaw |
|---|---|
| **Binary** | < 2 MB |
| **RAM** | < 5 MB |
| **Startup** | < 1 second |
| **Language** | C (~5,000 lines) |
| **Hardware** | GPIO, I2C, SPI, Camera |
| **Sandbox** | Native Linux namespaces |
| **Web UI** | Embedded in binary |
| **Offline** | llama.cpp fallback |
| **Agent network** | ASAP Protocol |

## Build and run

**Build:** `make shellclaw` → binary at `build/shellclaw`. `make test` → builds and runs all tests in `build/`.

**Run:** `./build/shellclaw`

**Quality checks:**
- `make static` — cppcheck on `src/` (requires cppcheck)
- `make coverage` — coverage report; fails if core < 80% (requires lcov)
- CI enforces release binary < 2 MB; optional `asap-compliance` when the Python package is available
- **Before opening a PR:** run `CI=true make clean && CI=true make test` (matches Linux CI with `-Werror`), or on a machine with the same apt deps as [.github/workflows/ci.yml](.github/workflows/ci.yml): `chmod +x scripts/ci-local.sh && ./scripts/ci-local.sh`

**Phase 3 configuration (optional):** registry and revocation URLs, Tavily API key name, and sandbox-related keys are documented in [`.env.example`](.env.example). Install **libwebsockets** (`pkg-config` must find it) to build the gateway and run `GATEWAY=1 make test_gateway_http`.

**Debug (macOS):** Symbols in `tests-dSYM/`. Use `lldb build/test_agent` then `settings set target.debug-file-search-path tests-dSYM`. Old `.dSYM` in repo root? Run `make clean-root-dsym`.

## Thread Safety

The **main agent loop** is single-threaded: memory, providers, channels, and tools keep much of their state in process-wide data initialized at startup. **Inbound HTTP/WebSocket paths** (for example ASAP `POST /asap` and the WebSocket chat dispatcher) may run on **libwebsockets worker threads**. Those code paths must call `agent_lock()` before `agent_run()` and `agent_unlock()` afterward so only one `agent_run` uses shared session/memory state at a time. Do not call `agent_run`, provider `chat`, or memory functions from arbitrary new threads without the same discipline.

## Architecture

```
Channels (Telegram, Discord, WebChat)
         │
         ▼
   ┌──────────────┐     ┌──────────────┐
   │  Agent Loop  │────►│  LLM APIs    │
   │  (ReAct)     │     │  (Claude,    │
   │              │◄────│   OpenAI,    │
   │              │     │   local)     │
   └──────┬───────┘     └──────────────┘
          │
   ┌──────▼───────┐     ┌──────────────┐
   │  Tools       │────►│  Hardware    │
   │  (shell,     │     │  (GPIO, I2C, │
   │   search,    │     │   camera)    │
   │   cron,      │     └──────────────┘
   │   file,      │
   │   asap)      │     ┌──────────────┐
   │              │────►│  ASAP Agent  │
   └──────────────┘     │  Ecosystem   │
                        └──────────────┘
```

## License

[MIT](LICENSE) — permissive, simple and aligned with the ASAP ecosystem and similar agents. Use, modify and distribute freely; keep the copyright notice.
