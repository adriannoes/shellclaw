# ShellClaw

**The first physical AI agent that participates in a global agent ecosystem.**

A lightweight AI assistant written in C, designed to run on a Raspberry Pi
Zero 2 W (~$15) and communicate with other agents through the
[ASAP Protocol](https://github.com/adriannoes/asap-protocol) and the
[agentic marketplace](https://asap-protocol.vercel.app/).

**Status:** Planning phase.

**Roadmap (high level):**

| Phase | Version | Focus |
|-------|---------|--------|
| 1 — Foundation | v0.1.0 | Core agent loop, CLI + Telegram, Anthropic/OpenAI, shell/search/file tools, SQLite memory & sessions, skill loading |
| 2 — Gateway | v0.2.0 | HTTP server, embedded Web UI, WebSocket chat, cron scheduler, pairing auth, ASAP manifest, skill hot-reload |
| 3 — Protocol | v0.3.0 | ASAP client/server, registry, `asap_invoke` tool, process sandbox (namespaces + cgroups), Tavily search |
| 4 — Autonomy | v0.4.0 | Local inference (llama.cpp), provider fallback, Discord channel, systemd service, OTA updates |
| 5 — Hardware & Release | v1.0.0 | GPIO, I2C sensors, camera, Ed25519 signing, ASAP marketplace registration, security audit, full docs |

Details: [PLAN.md](.cursor/strategy/PLAN.md) (architecture), [PRDs](.cursor/product-specs/prd/) (per-phase requirements).

## What makes ShellClaw different

ShellClaw is not another OpenClaw clone in a different language. It is a
**hardware-native agent** that interacts with the physical world (GPIO, I2C
sensors, camera) while collaborating with cloud-based agents through a
standardized protocol.

| Feature | ShellClaw |
|---|---|
| **Binary** | ~400 KB |
| **RAM** | < 5 MB |
| **Startup** | < 1 second |
| **Language** | C (~4,000 lines) |
| **Hardware** | GPIO, I2C, SPI, Camera |
| **Sandbox** | Native Linux namespaces |
| **Web UI** | Embedded in binary |
| **Offline** | llama.cpp fallback |
| **Agent network** | ASAP Protocol |

## Build layout

- **`build/`** — Binaries: `build/shellclaw` and test executables (`build/test_config`, `build/test_agent`, etc.). Created by `make shellclaw` and `make test`.
- **`tests-dSYM/`** — Debug symbol bundles (macOS only, debug builds). All `.dSYM` folders are generated here so the repo root stays clean. Use `make clean` to remove. If you have old `.dSYM` folders in the repo root (e.g. `test_agent.dSYM`), run `make clean` or `make clean-root-dsym` to remove them.

To run the main binary: `./build/shellclaw`. To run tests: `make test` (executes all tests from `build/`). To debug with lldb and symbols from `tests-dSYM/`: e.g. `lldb build/test_agent` then `settings set target.debug-file-search-path tests-dSYM`.

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