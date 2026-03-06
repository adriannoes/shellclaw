# ShellClaw: Architecture & Implementation Plan

> **The first physical AI agent that participates in a global agent ecosystem.**

**Version:** 0.1.0-draft
**Date:** 2026-03-06
**Status:** Planning
**License:** MIT (planned)

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Problem Statement](#2-problem-statement)
3. [Design Philosophy](#3-design-philosophy)
4. [Hardware Selection](#4-hardware-selection)
5. [Software Architecture](#5-software-architecture)
6. [Module Specification](#6-module-specification)
7. [ASAP Protocol Integration](#7-asap-protocol-integration)
8. [Security Model](#8-security-model)
9. [Embedded Web UI](#9-embedded-web-ui)
10. [Project Structure](#10-project-structure)
11. [Dependencies](#11-dependencies)
12. [Build System](#12-build-system)
13. [Testing Strategy](#13-testing-strategy)
14. [Roadmap](#14-roadmap)
15. [Comparison with Existing Solutions](#15-comparison-with-existing-solutions)
16. [Open Questions](#16-open-questions)
17. [References](#17-references)

---

## 1. Executive Summary

ShellClaw is a lightweight AI assistant written in C, designed to run on a
Raspberry Pi Zero 2 W (~$15) and participate in the
[ASAP Protocol](https://github.com/adriannoes/asap-protocol) global agent
ecosystem.

Unlike existing OpenClaw alternatives that replicate the same "chatbot that
calls APIs" pattern in different languages (Rust, Go, Python, TypeScript),
ShellClaw takes a fundamentally different approach: it is a **hardware-native
agent** that interacts with the physical world (GPIO, I2C sensors, camera) while
collaborating with cloud-based agents through a standardized protocol.

### What makes ShellClaw different

| Dimension | Existing ecosystem | ShellClaw |
|---|---|---|
| **Identity** | Software process in a cloud/desktop | Physical device you can touch |
| **Scope** | Isolated assistant | Agent in a global ecosystem (ASAP) |
| **Hardware** | Runs *on* hardware | *Is* hardware (GPIO, sensors, camera) |
| **Offline** | Dead without internet | Local inference fallback (llama.cpp) |
| **Sandbox** | Docker containers or none | Native Linux namespaces (zero overhead) |
| **Frontend** | External apps or none | Self-hosted embedded Web UI |
| **Skills** | Static config files | Live-reloading via inotify() |

### Key metrics (targets)

| Metric | Target |
|---|---|
| Binary size | < 500 KB |
| RAM usage (idle) | < 5 MB |
| RAM usage (active) | < 15 MB |
| Cold start | < 1 second |
| Total C lines (excl. vendor) | < 5,000 |
| Hardware cost | ~$15 (RPi Zero 2 W) |
| Dependencies (system) | 3 (libcurl, libmicrohttpd, libwebsockets) |
| Dependencies (vendored) | 3 (cJSON, SQLite, toml-c) |

---

## 2. Problem Statement

### 2.1 The "clone fatigue" problem

The OpenClaw ecosystem has spawned numerous alternatives, each rewriting the
same core loop (read message → call LLM → execute tools → respond) in a
different language:

- **OpenClaw** — TypeScript, ~430,000 lines, >1 GB RAM
- **ZeroClaw** — Rust, ~15,000 lines, <5 MB RAM
- **PicoClaw** — Go, ~8,000 lines, <10 MB RAM
- **NanoClaw** — TypeScript, container-focused
- **TinyClaw** — TypeScript, multi-agent via tmux
- **MimiClaw** — C, ESP32-S3 bare-metal firmware
- **SubZeroClaw** — C, ~380 lines, radical minimalism

Each optimizes a different axis (size, speed, cost, security) but all share the
same fundamental limitation: **they are isolated islands**. An agent running on
one system cannot discover, trust, or collaborate with an agent on another
system in a standardized way.

### 2.2 The missing physical layer

All existing solutions treat hardware as a platform to run on, not as a
capability to expose. None of them can:

- Read a temperature sensor and act on the data
- Control a relay based on an LLM decision
- Capture a camera image and send it to another agent
- Survive an internet outage with local inference

### 2.3 The interoperability gap

There is no standard way for a PicoClaw agent on a LicheeRV-Nano to ask a
ZeroClaw agent on a VPS to perform a web search. Each ecosystem is a silo.

The [ASAP Protocol](https://github.com/adriannoes/asap-protocol) solves this
with a production-ready agent-to-agent communication layer, but it currently
has no implementation outside Python. ShellClaw would be the first non-Python,
hardware-native ASAP agent — validating the protocol's language-agnostic design.

---

## 3. Design Philosophy

### 3.1 Core principles

1. **Hardware is a first-class capability, not just a platform.**
   The agent doesn't just run on a Raspberry Pi — it *is* the Raspberry Pi.
   GPIO pins, I2C buses, and camera modules are tools the LLM can invoke.

2. **Agents should talk to agents.**
   Via ASAP Protocol, ShellClaw participates in a global mesh of agents that
   can discover, trust, and delegate work to each other.

3. **The shell is the universal adapter.**
   Instead of building integrations for git, curl, ffmpeg, or ImageMagick, we
   expose `popen()` with sandboxing. Every CLI tool on the system becomes an
   agent capability. This is the SubZeroClaw insight — and it's correct.

4. **Security through Linux primitives, not application logic.**
   No Docker daemon. No container runtime. Raw `clone()`, `unshare()`, and
   cgroups v2 provide OS-level isolation with zero overhead.

5. **Survive without the cloud.**
   Local inference via llama.cpp HTTP bridge means the agent continues
   functioning (with reduced capability) when internet connectivity is lost.

6. **Small enough to audit.**
   The entire codebase should be readable in an afternoon. If you can't
   understand the security model, the security model doesn't work.

### 3.2 What we explicitly do NOT build

- **Plugin/extension system** — Skills are markdown files. Code changes require
  recompilation. This is intentional: if the codebase is small enough to modify,
  a plugin system adds complexity without value.

- **Multi-user support** — ShellClaw is a personal agent. One device, one owner.
  Multi-tenancy is delegated to the ASAP Protocol layer (multiple ShellClaw
  instances can coexist in the same ecosystem).

- **GUI desktop app** — The embedded Web UI serves configuration and monitoring.
  There is no Electron app, no menu bar widget, no system tray.

- **Package manager for skills** — `cp skill.md ~/.shellclaw/skills/` is the
  package manager. Skills are plain text.

### 3.3 Architectural influences

| Source | What we take | What we leave behind |
|---|---|---|
| **SubZeroClaw** | Radical simplicity, shell-as-tool, skills-as-markdown | No architecture at all (single file, no modularity) |
| **MimiClaw** | Proof that C works for AI agents, multi-provider, SPIFFS memory | Bare-metal ESP32 (we use Linux for richer capabilities) |
| **ZeroClaw** | Trait-based architecture (vtables), security checklist | Rust complexity, cargo dependency tree |
| **PicoClaw** | Channel router pattern, model_list config, cron scheduler | Go runtime overhead |
| **OpenClaw** | Gateway WebSocket protocol, Control UI concept, skills platform | 430,000 lines of TypeScript |
| **ASAP Protocol** | Agent-to-agent communication, state machine, MCP envelope | Python-only implementation |

---

## 4. Hardware Selection

### 4.1 Why Raspberry Pi Zero 2 W

| Criterion | RPi Zero 2 W | ESP32-S3 | LicheeRV-Nano | Arduino Uno |
|---|---|---|---|---|
| **CPU** | 4× Cortex-A53 @ 1 GHz | 2× Xtensa @ 240 MHz | 1× RISC-V @ 1 GHz | 1× AVR @ 16 MHz |
| **RAM** | 512 MB | 512 KB + 8 MB PSRAM | 64–256 MB | 2 KB |
| **Storage** | microSD (unlimited) | 16 MB flash | microSD | 32 KB flash |
| **WiFi** | 802.11 b/g/n | 802.11 b/g/n | Optional (Ethernet) | None |
| **Bluetooth** | BLE 4.2 | BLE 5.0 | None | None |
| **OS** | Linux (full) | None (bare-metal) | Linux (minimal) | None |
| **GPIO** | 40 pins | 45 pins | Limited | 14 digital + 6 analog |
| **Camera** | CSI (libcamera) | Limited | MIPI CSI | None |
| **I2C/SPI** | Full support | Full support | Full support | Full support |
| **Price** | ~$15 | ~$5–10 | ~$10 | ~$25 |
| **TLS/HTTPS** | Native (OpenSSL) | mbedTLS (limited) | Native | Impossible |
| **JSON parsing** | No limit | ~8 MB max | Depends on RAM | Impossible |
| **SQLite** | Full support | Not practical | Possible | Impossible |
| **llama.cpp** | Possible (slow) | Impossible | Possible (very slow) | Impossible |
| **Process isolation** | cgroups + namespaces | None | Partial | None |
| **Web UI server** | Full HTTP/WS | Basic HTTP | Full HTTP/WS | None |

**Decision:** Raspberry Pi Zero 2 W.

**Rationale:**

1. **Linux gives us the shell.** The SubZeroClaw insight is that `popen()` is
   the universal tool adapter. This requires a Unix shell, which ESP32 cannot
   provide.

2. **512 MB RAM enables local inference.** TinyLlama 1.1B Q4_K_M requires
   ~670 MB model + ~200 MB runtime. With swap on microSD, the RPi Zero 2 W can
   run it (slowly). ESP32's 8 MB PSRAM cannot.

3. **Process isolation requires Linux namespaces.** Our security model depends
   on `clone(CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWNET)` and cgroups v2. This
   is a Linux kernel feature.

4. **Camera via libcamera.** The RPi camera module is well-supported, enabling
   vision capabilities (capture → base64 → send to multimodal LLM).

5. **microSD means unlimited storage.** Session history, memory database, logs,
   and OTA updates are not constrained by flash size.

6. **$15 is still cheap.** While $5 for an ESP32 is attractive, $15 for a
   quad-core Linux computer with WiFi is exceptional value.

### 4.2 Why NOT Arduino

Arduino (AVR-based: Uno, Mega, Nano) is fundamentally unsuitable:

- **2 KB RAM** cannot hold a single HTTP response from an LLM API.
- **No WiFi** — requires expensive shields that consume most of the available
  RAM for the TCP/IP stack alone.
- **No TLS** — all LLM APIs require HTTPS. The TLS handshake alone needs more
  memory than the entire chip provides.
- **No filesystem** — no way to store config, memory, sessions, or skills.
- **No multitasking** — cannot poll Telegram while processing a tool call.

Arduino ARM boards (Due, Zero) improve on RAM (96 KB) but still lack WiFi, TLS,
filesystem, and process isolation.

### 4.3 Future hardware targets

ShellClaw is designed to be portable across Linux SBCs. The primary target is
RPi Zero 2 W, but the code should compile and run on:

| Board | Notes |
|---|---|
| Raspberry Pi Zero 2 W | Primary target |
| Raspberry Pi 3/4/5 | More RAM/CPU, same GPIO |
| Orange Pi Zero 3 | Cheaper, 1–4 GB RAM |
| Milk-V Duo (256M) | RISC-V, very compact |
| Banana Pi M2 Zero | RPi Zero pin-compatible |
| Any Linux x86/ARM | Desktop/server (no GPIO) |

---

## 5. Software Architecture

### 5.1 High-level architecture

```
                    ┌─────────────────────────────────┐
                    │          User / World            │
                    └──────┬──────────────┬────────────┘
                           │              │
              Channels     │              │  Physical
              (Telegram,   │              │  (GPIO,
               Discord,    │              │   sensors,
               WebChat)    │              │   camera)
                           │              │
                    ┌──────▼──────────────▼────────────┐
                    │         shellclaw daemon          │
                    │                                   │
                    │  ┌─────────┐    ┌──────────────┐ │
                    │  │ Channel │    │   Hardware    │ │
                    │  │ Router  │    │   Bridge     │ │
                    │  └────┬────┘    └──────┬───────┘ │
                    │       │               │          │
                    │  ┌────▼───────────────▼───────┐  │
                    │  │        Agent Loop          │  │
                    │  │    (ReAct: think → act)    │  │
                    │  └────┬──────────┬────────────┘  │
                    │       │          │               │
                    │  ┌────▼────┐ ┌───▼────────────┐  │
                    │  │Provider │ │  Tool Registry  │  │
                    │  │ Router  │ │  (shell, web,   │  │
                    │  │         │ │   cron, file,   │  │
                    │  │ Claude  │ │   gpio, camera, │  │
                    │  │ OpenAI  │ │   asap_invoke)  │  │
                    │  │ Local   │ └────────────────┘  │
                    │  └─────────┘                     │
                    │                                   │
                    │  ┌─────────┐  ┌──────────────┐   │
                    │  │ Memory  │  │ ASAP Client/  │   │
                    │  │ (SQLite)│  │ Server        │   │
                    │  └─────────┘  └──────────────┘   │
                    │                                   │
                    │  ┌─────────────────────────────┐  │
                    │  │  HTTP Gateway (:18789)      │  │
                    │  │  ├── Web UI (embedded)      │  │
                    │  │  ├── WebSocket (chat)       │  │
                    │  │  ├── /asap (JSON-RPC)       │  │
                    │  │  ├── /.well-known/asap/     │  │
                    │  │  └── /health                │  │
                    │  └─────────────────────────────┘  │
                    └───────────────────────────────────┘
```

### 5.2 Vtable architecture (C "traits")

Every swappable subsystem is defined as a struct of function pointers, inspired
by ZeroClaw's trait-based design but implemented in idiomatic C:

```c
// --- Provider (LLM API) ---
typedef struct {
    const char *name;
    int  (*init)(const config_t *cfg);
    int  (*chat)(const message_t *msgs, int count,
                 const tool_def_t *tools, int tool_count,
                 response_t *out);
    void (*cleanup)(void);
} provider_t;

// --- Channel (messaging surface) ---
typedef struct {
    const char *name;
    int  (*init)(const config_t *cfg);
    int  (*poll)(incoming_msg_t *out, int timeout_ms);
    int  (*send)(const char *recipient, const char *text,
                 const attachment_t *attachments, int att_count);
    void (*cleanup)(void);
} channel_t;

// --- Tool (agent capability) ---
typedef struct {
    const char *name;
    const char *description;
    const char *parameters_json;  // JSON Schema for LLM
    int  (*execute)(const char *args_json, char *result, size_t max_len);
} tool_t;

// --- Memory (persistence) ---
typedef struct {
    const char *name;
    int  (*init)(const char *path);
    int  (*save)(const char *key, const char *content, const char *metadata);
    int  (*recall)(const char *query, char *results, size_t max_len, int limit);
    int  (*list)(char *results, size_t max_len, int offset, int limit);
    void (*cleanup)(void);
} memory_t;
```

**Rationale:** Function pointer structs are the standard C pattern for
polymorphism. They provide the same extensibility as Rust traits or Go
interfaces with zero runtime overhead (direct function calls, no dynamic
dispatch tables, no vtable lookups — the compiler can even inline).

### 5.3 Agent loop (ReAct pattern)

The core agent loop follows the ReAct (Reason + Act) pattern, identical in
concept to SubZeroClaw's loop but with proper error handling and tool streaming:

```
1. Receive message (from channel, heartbeat, or ASAP envelope)
2. Load context:
   a. System prompt (SOUL.md + IDENTITY.md + active skills)
   b. Session history (from SQLite)
   c. Relevant memories (from SQLite FTS5 search)
3. Send to LLM provider (with tool definitions)
4. If response contains tool calls:
   a. Execute each tool (with sandboxing if shell/file)
   b. Append tool results to context
   c. Go to step 3 (loop until no more tool calls or max_iterations)
5. Send final response back through the originating channel
6. Save to session history
7. Optionally save to long-term memory
```

**Max iterations:** Configurable (default: 20). Prevents infinite tool-calling
loops. After max iterations, the agent responds with whatever it has.

### 5.4 Data flow for an ASAP-delegated task

```
External ASAP Agent                    ShellClaw
       │                                  │
       │  POST /asap                      │
       │  { "jsonrpc": "2.0",             │
       │    "method": "asap.send",        │
       │    "params": {                   │
       │      "envelope": {               │
       │        "sender": "urn:asap:      │
       │          agent:cloud-research",  │
       │        "recipient": "urn:asap:   │
       │          agent:shellclaw-01",    │
       │        "payload_type":           │
       │          "task.request",         │
       │        "payload": {              │
       │          "skill_id":             │
       │            "sensor_read",        │
       │          "input": {              │
       │            "device": "bme280"    │
       │          }                       │
       │        }                         │
       │      }                           │
       │    }                             │
       │  }                               │
       │ ─────────────────────────────►   │
       │                                  │ 1. Parse envelope
       │                                  │ 2. Validate sender trust
       │                                  │ 3. Route to agent loop
       │                                  │ 4. Agent calls sensor_read tool
       │                                  │ 5. I2C read → BME280 data
       │                                  │ 6. Format response
       │                                  │
       │  200 OK                          │
       │  { "jsonrpc": "2.0",             │
       │    "result": {                   │
       │      "envelope": {               │
       │        "payload_type":           │
       │          "task.response",        │
       │        "payload": {              │
       │          "status": "completed",  │
       │          "result": {             │
       │            "temperature": 23.5,  │
       │            "humidity": 45,       │
       │            "pressure": 1013      │
       │          }                       │
       │        }                         │
       │      }                           │
       │    }                             │
       │  }                               │
       │ ◄─────────────────────────────   │
```

---

## 6. Module Specification

### 6.1 Core Runtime (`src/core/`)

#### `main.c` — Entry point (~100 lines)

Responsibilities:
- Parse CLI arguments (`--config`, `--port`, `--verbose`, `--daemon`)
- Load configuration
- Initialize all subsystems
- Register signal handlers (SIGTERM, SIGINT for graceful shutdown)
- Start main event loop
- Fork to background if `--daemon`

#### `config.c` — Configuration (~200 lines)

Configuration file: `~/.shellclaw/config.toml`

```toml
[agent]
model = "anthropic/claude-sonnet-4-20250514"
max_tokens = 4096
temperature = 0.7
max_tool_iterations = 20

[agent.identity]
soul = "~/.shellclaw/SOUL.md"
identity = "~/.shellclaw/IDENTITY.md"
user = "~/.shellclaw/USER.md"

[providers.anthropic]
api_key_env = "ANTHROPIC_API_KEY"

[providers.openai]
api_key_env = "OPENAI_API_KEY"

[providers.openrouter]
api_key_env = "OPENROUTER_API_KEY"

[providers.local]
endpoint = "http://127.0.0.1:8080/v1/chat/completions"
model = "tinyllama-1.1b-q4"

[channels.telegram]
enabled = true
token_env = "TELEGRAM_BOT_TOKEN"
allowed_users = ["123456789"]

[channels.discord]
enabled = false
token_env = "DISCORD_BOT_TOKEN"
allowed_users = []

[gateway]
host = "127.0.0.1"
port = 18789
require_pairing = true

[memory]
backend = "sqlite"
db_path = "~/.shellclaw/memory.db"

[sandbox]
enabled = true
memory_limit_mb = 64
cpu_limit_percent = 50
network = false
workspace_only = true

[heartbeat]
enabled = true
interval_minutes = 30

[skills]
dir = "~/.shellclaw/skills"
hot_reload = true

[asap]
enabled = true
agent_urn = "urn:asap:agent:shellclaw-home-01"
agent_name = "ShellClaw Home Agent"
registry_url = "https://raw.githubusercontent.com/adriannoes/asap-protocol/main/registry.json"

[hardware]
gpio_enabled = true
i2c_enabled = true
camera_enabled = false
```

Parsing: Vendored TOML parser. Environment variables override file values.
Runtime: Config is read-only after initialization (no locking needed).

#### `agent.c` — Agent loop (~300 lines)

The ReAct loop implementation. Manages:
- Context window assembly (system prompt + history + memories)
- Provider dispatch (select provider based on config + fallback logic)
- Tool call parsing and execution
- Response routing back to originating channel
- Session persistence
- Context compaction when history exceeds `max_context_messages` (default: 40)

Context compaction follows SubZeroClaw's approach: send old messages to the LLM
for summarization, replace with summary, keep last N messages raw.

#### `skill.c` — Skill loader (~150 lines)

Skills are markdown files in `~/.shellclaw/skills/`. Each file is read into
the system prompt verbatim.

Hot-reload via `inotify_init()` + `inotify_add_watch()`:
- `IN_CREATE` / `IN_MODIFY` → reload skill into memory
- `IN_DELETE` → remove skill from active set
- `IN_MOVED_TO` / `IN_MOVED_FROM` → handle renames

No special format. No metadata headers. No YAML frontmatter. The LLM reads
the markdown and figures out what to do. This is the SubZeroClaw philosophy:
the skill is just text the LLM reads.

#### `memory.c` — SQLite memory store (~250 lines)

Schema:

```sql
CREATE TABLE memories (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    key         TEXT NOT NULL,
    content     TEXT NOT NULL,
    metadata    TEXT,  -- JSON
    created_at  TEXT DEFAULT (datetime('now')),
    updated_at  TEXT DEFAULT (datetime('now'))
);

CREATE VIRTUAL TABLE memories_fts USING fts5(
    key, content, metadata,
    content=memories,
    content_rowid=id
);

CREATE TABLE sessions (
    id          TEXT PRIMARY KEY,   -- channel:user_id
    messages    TEXT NOT NULL,      -- JSON array
    created_at  TEXT DEFAULT (datetime('now')),
    updated_at  TEXT DEFAULT (datetime('now'))
);

CREATE TABLE cron_jobs (
    id          TEXT PRIMARY KEY,
    schedule    TEXT NOT NULL,      -- cron expression or "every Ns"
    message     TEXT NOT NULL,
    channel     TEXT,
    recipient   TEXT,
    next_run    INTEGER NOT NULL,   -- unix timestamp
    enabled     INTEGER DEFAULT 1
);

CREATE TABLE config_kv (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
```

**FTS5** provides full-text search over memories with BM25 scoring.
No vector embeddings in v0.1 — keyword search is sufficient for a personal
agent with hundreds (not millions) of memories.

### 6.2 Providers (`src/providers/`)

#### `provider.h` — Interface

Defined in section 5.2. All providers implement the same vtable.

#### `anthropic.c` — Claude API (~200 lines)

- Endpoint: `https://api.anthropic.com/v1/messages`
- Auth: `x-api-key` header
- Tool calling: Native Anthropic tool_use blocks
- Streaming: Not in v0.1 (buffered response)

#### `openai.c` — OpenAI-compatible (~200 lines)

- Endpoint: Configurable (default: `https://api.openai.com/v1/chat/completions`)
- Auth: `Authorization: Bearer` header
- Tool calling: OpenAI function calling format
- Reused for: OpenRouter, DeepSeek, Groq, local llama.cpp, any
  OpenAI-compatible endpoint

#### `local.c` — llama.cpp bridge (~100 lines)

- Endpoint: `http://127.0.0.1:8080/v1/chat/completions` (llama-server)
- Same OpenAI-compatible format
- Used as automatic fallback when primary provider is unreachable
- The llama-server process is managed externally (systemd service)

#### `router.c` — Provider selection (~100 lines)

Selection logic:
1. If ASAP envelope specifies a model → use that provider
2. If session has a provider override → use that
3. Try configured default provider
4. If request fails (timeout/5xx) → try next provider in `fallback_order`
5. If all cloud providers fail → try local provider (if configured)
6. If local fails → return error to user

### 6.3 Channels (`src/channels/`)

#### `cli.c` — stdin/stdout (~50 lines)

Interactive mode. Reads lines from stdin, sends to agent loop, prints response.
Used for development and direct terminal interaction.

#### `telegram.c` — Telegram Bot API (~200 lines)

- Long polling via `getUpdates` (no webhook server needed)
- Supports: text messages, photo captions, voice (if transcription configured)
- Sender allowlist from config
- Media: photos sent as base64 to multimodal LLMs
- Commands: `/reset` (clear session), `/status` (show status)

#### `websocket.c` — WebSocket gateway (~150 lines)

- Path: `ws://host:18789/ws`
- JSON frames: `{ "type": "message", "text": "..." }`
- Used by the embedded Web UI for the chat interface
- Pairing code authentication on first connect

#### `discord.c` — Discord gateway (~250 lines)

- WebSocket connection to Discord gateway
- Bot token authentication
- Intents: MESSAGE_CONTENT, GUILDS
- Slash commands not required (responds to mentions and DMs)

### 6.4 Tools (`src/tools/`)

#### `shell.c` — Shell execution (~100 lines)

- `popen()` with configurable timeout (default: 60s)
- When sandbox is enabled: executed inside namespaced process
- Blocked commands: `rm -rf /`, `mkfs`, `dd if=`, `shutdown`, fork bombs
- Path restriction: when `workspace_only = true`, commands are rejected if
  they reference paths outside the workspace

#### `web_search.c` — Web search (~100 lines)

Provider priority (first available wins):
1. Tavily (if API key configured) — best for AI agents
2. Brave Search (if API key configured)
3. DuckDuckGo (built-in, no API key needed)

#### `cron.c` — Task scheduler (~150 lines)

- Stores jobs in SQLite `cron_jobs` table
- Supports: cron expressions (`0 9 * * 1-5`), intervals (`every 3600s`),
  one-shot (`at 1709856000`)
- On trigger: injects message into agent loop
- Agent can create/list/remove cron jobs via tool calls

#### `file.c` — File operations (~100 lines)

- `read_file(path)`, `write_file(path, content)`, `list_dir(path)`
- Path validation: must be within workspace (when `workspace_only = true`)
- Symlink resolution: canonicalize path before checking workspace boundary

#### `hardware.c` — Hardware bridge (~200 lines)

GPIO (via `/sys/class/gpio/` sysfs or libgpiod):
- `gpio_read(pin)` → returns "HIGH" or "LOW"
- `gpio_write(pin, value)` → sets pin HIGH/LOW
- `gpio_mode(pin, mode)` → sets INPUT/OUTPUT

I2C (via `/dev/i2c-*`):
- `i2c_read(bus, addr, reg, len)` → returns hex bytes
- `i2c_write(bus, addr, reg, data)` → writes bytes
- `sensor_read(device_name)` → high-level read for known sensors
  (BME280, DHT22, BH1750, etc.)

Camera (via `libcamera-still` CLI):
- `camera_capture(output_path)` → captures image
- Returns base64-encoded image for multimodal LLMs

#### `asap_invoke.c` — ASAP delegation (~100 lines)

- `asap_invoke(urn, skill_id, input_json)` → calls external ASAP agent
- Looks up agent in cached registry
- Sends JSON-RPC 2.0 envelope to agent's HTTP endpoint
- Returns task response

### 6.5 Sandbox (`src/sandbox/`)

#### `sandbox.c` — Process isolation (~200 lines)

When enabled, shell commands and file operations execute inside an isolated
Linux process:

```c
int sandbox_exec(const char *cmd, char *output, size_t max_len) {
    // 1. Create pipe for stdout/stderr capture
    // 2. clone() with CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWNET
    // 3. In child:
    //    a. Mount tmpfs over sensitive dirs (/etc/shadow, ~/.ssh, etc.)
    //    b. chdir to workspace
    //    c. Set resource limits via cgroups v2:
    //       - memory.max = config.sandbox.memory_limit_mb
    //       - cpu.max = config.sandbox.cpu_limit_percent
    //    d. Drop all capabilities except CAP_NET_RAW (if network enabled)
    //    e. execl("/bin/sh", "-c", cmd)
    // 4. In parent:
    //    a. Read pipe into output buffer
    //    b. waitpid() with timeout
    //    c. Kill child if timeout exceeded
    //    d. Cleanup cgroup
}
```

**Overhead:** Measured at <1ms for `clone()` + namespace setup. Compare with
Docker container startup: ~500ms–2s.

**Capabilities dropped:** All except `CAP_NET_RAW` (needed for ping). The
sandboxed process cannot:
- Access files outside the workspace mount
- See host processes (PID namespace)
- Access the network (unless explicitly allowed)
- Use more memory/CPU than configured limits
- Escalate privileges

### 6.6 Gateway (`src/gateway/`)

#### `http.c` — HTTP server (~200 lines)

Using libmicrohttpd (GNU LGPL, battle-tested, minimal):

| Endpoint | Method | Auth | Purpose |
|---|---|---|---|
| `/health` | GET | None | Health check (`{"status":"ok","uptime":...}`) |
| `/pair` | POST | Pairing code | Exchange code for bearer token |
| `/api/config` | GET/PUT | Bearer | Read/update configuration |
| `/api/skills` | GET/POST/DELETE | Bearer | Manage skills |
| `/api/memory` | GET | Bearer | Search memories |
| `/api/sessions` | GET/DELETE | Bearer | List/clear sessions |
| `/api/cron` | GET/POST/DELETE | Bearer | Manage cron jobs |
| `/api/hardware` | GET | Bearer | Hardware status (GPIO, sensors) |
| `/asap` | POST | Bearer/ASAP | ASAP JSON-RPC endpoint |
| `/.well-known/asap/manifest.json` | GET | None | ASAP manifest |
| `/.well-known/asap/health` | GET | None | ASAP health |
| `/*` | GET | None | Serve embedded Web UI |

#### `ws.c` — WebSocket server (~100 lines)

Using libwebsockets:

- Path: `/ws` — WebChat (JSON messages)
- Path: `/asap/ws` — ASAP WebSocket (JSON-RPC envelopes)
- Heartbeat: ping/pong every 30s
- Authentication: Bearer token in first message or query param

#### `static.c` — Embedded UI server (~50 lines)

The Web UI (HTML/CSS/JS) is:
1. Developed as normal files in `web/` directory
2. Minified + gzip'd by build script
3. Converted to C byte arrays: `const unsigned char ui_index_html_gz[] = { ... };`
4. Compiled into the binary
5. Served with `Content-Encoding: gzip` header

Zero external dependencies at runtime. Zero npm. Zero Node.js.

---

## 7. ASAP Protocol Integration

### 7.1 Why ASAP

The ASAP Protocol (Async Simple Agent Protocol) provides:

1. **Standardized envelope format** — JSON-RPC 2.0 with typed payloads
   (task.request, task.response, mcp.tool_call, etc.)
2. **Agent discovery** — Registry with manifest URIs, skills, capabilities
3. **Task state machine** — SUBMITTED → WORKING → COMPLETED/FAILED/CANCELLED
4. **MCP compatibility** — Tool calls and results wrapped in ASAP envelopes
5. **Trust and security** — Ed25519 signed manifests, trust levels
6. **Observability** — trace_id and correlation_id for distributed tracing

No other agent communication protocol provides all of these in a single spec.

### 7.2 ShellClaw as ASAP agent

ShellClaw exposes itself as a first-class ASAP agent:

**Manifest** (`/.well-known/asap/manifest.json`):

```json
{
  "id": "urn:asap:agent:shellclaw-home-01",
  "name": "ShellClaw Home Agent",
  "version": "0.1.0",
  "description": "Hardware-native AI agent on Raspberry Pi Zero 2 W with GPIO, I2C sensors, and camera access.",
  "asap_version": "2.1",
  "capabilities": ["task_execution", "mcp_tools", "state_snapshots"],
  "skills": [
    {
      "id": "sensor_read",
      "name": "Read Hardware Sensors",
      "description": "Read temperature, humidity, pressure from I2C sensors (BME280, DHT22, BH1750)"
    },
    {
      "id": "gpio_control",
      "name": "Control GPIO Pins",
      "description": "Read and write GPIO pins on Raspberry Pi header"
    },
    {
      "id": "camera_capture",
      "name": "Capture Camera Image",
      "description": "Capture image from CSI camera module"
    },
    {
      "id": "shell_exec",
      "name": "Execute Shell Command",
      "description": "Run sandboxed shell commands on the host system"
    },
    {
      "id": "home_monitor",
      "name": "Home Monitoring",
      "description": "Continuous monitoring of sensors with alerting"
    }
  ],
  "endpoints": {
    "http": "https://shellclaw.tailnet-xxxx.ts.net/asap",
    "manifest": "https://shellclaw.tailnet-xxxx.ts.net/.well-known/asap/manifest.json"
  },
  "auth": {
    "schemes": ["bearer"]
  }
}
```

### 7.3 ASAP module in C (`src/asap/`)

#### `envelope.c` — Envelope handling (~150 lines)

```c
typedef struct {
    char id[32];
    char asap_version[8];
    char sender[128];
    char recipient[128];
    char payload_type[32];
    cJSON *payload;
    char correlation_id[32];
    char trace_id[32];
    char timestamp[32];
} asap_envelope_t;

// Parse JSON-RPC request into envelope
int asap_envelope_parse(const char *jsonrpc_body, asap_envelope_t *env);

// Build JSON-RPC response from envelope
int asap_envelope_to_jsonrpc(const asap_envelope_t *env, char *out, size_t max);

// Generate ULID for envelope/task IDs
void asap_ulid_generate(char *out, size_t max);
```

#### `client.c` — Outbound ASAP calls (~100 lines)

Used by the `asap_invoke` tool to delegate tasks to external agents:

```c
// Send task.request to an external agent
int asap_client_send_task(const char *endpoint, const char *recipient_urn,
                          const char *skill_id, const char *input_json,
                          asap_envelope_t *response);

// Discover agents from registry
int asap_client_fetch_registry(const char *registry_url,
                               asap_agent_info_t *agents, int max_agents);
```

#### `server.c` — Inbound ASAP handler (~100 lines)

Handles incoming ASAP envelopes on the `/asap` endpoint:

```c
// Route incoming envelope to appropriate handler
int asap_server_handle(const asap_envelope_t *env, asap_envelope_t *response);
```

Routing logic:
- `task.request` → Create task, run agent loop, return `task.response`
- `task.cancel` → Cancel running task
- `state.query` → Return state snapshot from SQLite
- `mcp.tool_call` → Execute tool directly, return `mcp.tool_result`

#### `manifest.c` — Manifest generation (~50 lines)

Builds the manifest JSON from config values. Served at
`/.well-known/asap/manifest.json`.

#### `registry.c` — Registry client (~80 lines)

- Fetches and caches `registry.json` (TTL: 5 minutes)
- Fetches and checks `revoked_agents.json` (no cache — checked before each call)
- Lookup agent by URN → returns endpoint URL

### 7.4 ASAP integration scenarios

#### Scenario 1: ShellClaw delegates to cloud agent

```
User → Telegram → ShellClaw:
  "Research the latest news about Raspberry Pi and summarize"

ShellClaw agent loop:
  1. Recognizes task requires web research
  2. Calls asap_invoke("urn:asap:agent:research-bot", "web_research",
     {"query": "latest Raspberry Pi news 2026"})
  3. ASAP client sends task.request to cloud agent
  4. Cloud agent performs web search, returns summary
  5. ShellClaw formats and sends response to Telegram
```

#### Scenario 2: Cloud agent delegates to ShellClaw

```
Cloud orchestrator → ASAP → ShellClaw:
  task.request: {
    "skill_id": "sensor_read",
    "input": {"device": "bme280", "interval_seconds": 60, "duration_minutes": 10}
  }

ShellClaw:
  1. Receives envelope on /asap endpoint
  2. Agent loop reads sensor every 60s for 10 minutes
  3. Returns aggregated data as task.response
```

#### Scenario 3: Multi-agent collaboration

```
User → ShellClaw: "Monitor server room temperature and alert if above 30°C"

ShellClaw:
  1. Creates cron job: read BME280 every 5 minutes
  2. On each read: if temp > 30°C:
     a. Local: gpio_write(relay_pin, HIGH) → activate cooling
     b. ASAP: asap_invoke("urn:asap:agent:notification-bot", "alert",
        {"message": "Server room temp: 32°C, cooling activated"})
     c. Notification bot → sends email/SMS/Slack
```

---

## 8. Security Model

### 8.1 Threat model

| Threat | Mitigation |
|---|---|
| LLM runs destructive command | Sandbox (namespaces + cgroups), blocked commands |
| Unauthorized Telegram user | Sender allowlist (deny-by-default) |
| Unauthorized ASAP caller | Bearer token + manifest trust verification |
| Gateway exposed to internet | Bind to 127.0.0.1 by default, refuse 0.0.0.0 without tunnel |
| API keys leaked in logs | Keys loaded from env vars, never logged |
| Path traversal in file tools | Canonicalize + workspace boundary check + symlink detection |
| Memory exhaustion | cgroups memory limit on sandboxed processes |
| CPU starvation | cgroups CPU limit on sandboxed processes |

### 8.2 Security defaults

```toml
[gateway]
host = "127.0.0.1"          # Loopback only — never exposed by default
require_pairing = true       # 6-digit code on first connect

[sandbox]
enabled = true               # All tool execution is sandboxed
workspace_only = true        # No access outside ~/.shellclaw/workspace/
network = false              # No network access for sandboxed commands
memory_limit_mb = 64
cpu_limit_percent = 50

[channels.telegram]
allowed_users = []           # Empty = deny all (must be explicitly configured)
```

### 8.3 Pairing flow

1. On first startup, ShellClaw generates a 6-digit pairing code
2. Code is displayed on stdout (and UART if connected)
3. Client sends `POST /pair` with `X-Pairing-Code: 123456`
4. Server returns a bearer token
5. All subsequent API calls require `Authorization: Bearer <token>`
6. Token is stored in `~/.shellclaw/auth_tokens.json`

### 8.4 ASAP trust (planned for v1.0)

- Verify Ed25519 signature on incoming agent manifests
- Check `revoked_agents.json` before processing any ASAP request
- Trust levels: `self_signed` (default), `community_verified`, `ca_signed`

---

## 9. Embedded Web UI

### 9.1 Design principles

- **No build step at runtime.** HTML/CSS/JS is pre-compiled into the C binary.
- **No external CDN.** All assets are self-contained.
- **No framework.** Vanilla HTML + CSS + minimal JS. Total size < 50 KB gzipped.
- **Terminal aesthetic.** Monospace fonts, dark theme, green-on-black accents.
  The UI should feel like it belongs on hardware.

### 9.2 Pages

| Path | Purpose |
|---|---|
| `/` | Dashboard: agent status, uptime, CPU/RAM/temp, active channels |
| `/chat` | WebChat: send messages to agent via WebSocket |
| `/config` | Configuration editor: form-based config.toml editing |
| `/skills` | Skill manager: list, view, create, edit, delete `.md` files |
| `/memory` | Memory browser: search and view agent memories |
| `/sessions` | Session viewer: list active sessions, view/clear history |
| `/cron` | Cron manager: list, create, pause, remove scheduled tasks |
| `/hardware` | Hardware status: GPIO pin states, sensor readings, camera preview |
| `/logs` | Live log stream via WebSocket |
| `/asap` | ASAP status: manifest, registered skills, recent envelopes |

### 9.3 Build pipeline

```
web/                          (source: ~20 KB uncompressed)
  ├── index.html
  ├── chat.html
  ├── config.html
  ├── ...
  ├── style.css
  └── app.js
        │
        ▼  scripts/embed_ui.sh
        │  (minify + gzip + xxd)
        ▼
src/gateway/ui_assets.h       (generated: const unsigned char arrays)
        │
        ▼  cc (compile)
        │
shellclaw binary              (UI embedded, served at /*)
```

The `embed_ui.sh` script:
1. Minifies HTML (remove comments, collapse whitespace)
2. Minifies CSS (csso or manual)
3. Minifies JS (terser or manual)
4. Gzip compresses each file
5. Converts to C header: `xxd -i file.html.gz > ui_assets.h`

---

## 10. Project Structure

```
shellclaw/
├── src/
│   ├── main.c                     # Entry point, CLI, daemon
│   ├── core/
│   │   ├── agent.c                # ReAct agent loop
│   │   ├── agent.h
│   │   ├── config.c               # TOML config parser
│   │   ├── config.h
│   │   ├── memory.c               # SQLite memory store
│   │   ├── memory.h
│   │   ├── skill.c                # Skill loader + inotify watcher
│   │   └── skill.h
│   ├── providers/
│   │   ├── provider.h             # Provider vtable interface
│   │   ├── anthropic.c            # Claude API
│   │   ├── openai.c               # OpenAI-compatible API
│   │   ├── local.c                # llama.cpp HTTP bridge
│   │   └── router.c               # Provider selection + fallback
│   ├── channels/
│   │   ├── channel.h              # Channel vtable interface
│   │   ├── cli.c                  # stdin/stdout
│   │   ├── telegram.c             # Telegram Bot API
│   │   ├── websocket.c            # WebSocket gateway
│   │   └── discord.c              # Discord gateway
│   ├── tools/
│   │   ├── tool.h                 # Tool vtable interface
│   │   ├── shell.c                # Shell execution (with sandbox)
│   │   ├── web_search.c           # Brave/Tavily/DuckDuckGo
│   │   ├── cron.c                 # Task scheduler
│   │   ├── file.c                 # File operations (sandboxed)
│   │   ├── hardware.c             # GPIO, I2C, camera
│   │   └── asap_invoke.c          # Delegate to external ASAP agent
│   ├── sandbox/
│   │   ├── sandbox.h
│   │   ├── sandbox.c              # clone(), namespaces, cgroups
│   │   └── allowlist.c            # Command/path allowlists
│   ├── asap/
│   │   ├── asap.h                 # ASAP types and interfaces
│   │   ├── envelope.c             # Envelope parse/build
│   │   ├── client.c               # Outbound ASAP calls
│   │   ├── server.c               # Inbound ASAP handler
│   │   ├── manifest.c             # Manifest generation
│   │   ├── registry.c             # Registry client + cache
│   │   └── ulid.c                 # ULID generator
│   ├── gateway/
│   │   ├── http.c                 # HTTP server (libmicrohttpd)
│   │   ├── ws.c                   # WebSocket server (libwebsockets)
│   │   ├── static.c               # Serve embedded UI assets
│   │   └── ui_assets.h            # Generated: embedded UI byte arrays
│   └── vendor/
│       ├── cJSON.c                # JSON parser (MIT, ~2000 lines)
│       ├── cJSON.h
│       ├── sqlite3.c              # SQLite amalgamation (~250 KB)
│       ├── sqlite3.h
│       ├── toml.c                 # TOML parser (~500 lines)
│       └── toml.h
├── web/                           # Web UI source (dev only)
│   ├── index.html                 # Dashboard
│   ├── chat.html                  # WebChat
│   ├── config.html                # Config editor
│   ├── skills.html                # Skill manager
│   ├── memory.html                # Memory browser
│   ├── hardware.html              # Hardware status
│   ├── logs.html                  # Log viewer
│   ├── asap.html                  # ASAP status
│   ├── style.css                  # Styles (terminal aesthetic)
│   └── app.js                     # Client-side logic
├── skills/                        # Example skills
│   ├── assistant.md               # General assistant
│   ├── home-monitor.md            # Home monitoring
│   ├── daily-briefing.md          # Daily news briefing
│   └── server-admin.md            # Server administration
├── scripts/
│   ├── build.sh                   # Build script
│   ├── embed_ui.sh                # Minify + embed Web UI
│   ├── install.sh                 # Install on RPi
│   ├── setup_pi.sh                # First-time RPi setup
│   └── cross_compile.sh           # Cross-compile from x86 to ARM
├── systemd/
│   ├── shellclaw.service          # systemd user service
│   └── llama-server.service       # llama.cpp server (optional)
├── tests/
│   ├── test_agent.c               # Agent loop tests
│   ├── test_config.c              # Config parser tests
│   ├── test_envelope.c            # ASAP envelope tests
│   ├── test_memory.c              # Memory store tests
│   ├── test_sandbox.c             # Sandbox isolation tests
│   └── test_tools.c               # Tool execution tests
├── docs/
│   ├── ARCHITECTURE.md            # Detailed architecture
│   ├── HARDWARE.md                # Hardware setup guide
│   ├── SECURITY.md                # Security model
│   └── ASAP.md                    # ASAP integration guide
├── config.example.toml            # Example configuration
├── .env.example                   # Example environment variables
├── Makefile                       # Primary build system
├── CMakeLists.txt                 # CMake (alternative)
├── LICENSE                        # MIT
├── CONTRIBUTING.md
├── CHANGELOG.md
└── README.md
```

**Estimated line counts:**

| Category | Files | Lines |
|---|---|---|
| Core (agent, config, memory, skill) | 8 | ~900 |
| Providers | 5 | ~600 |
| Channels | 5 | ~650 |
| Tools | 7 | ~750 |
| Sandbox | 3 | ~300 |
| ASAP | 7 | ~530 |
| Gateway | 4 | ~350 |
| **Total (excl. vendor, tests, web)** | **39** | **~4,080** |
| Vendor (cJSON, SQLite, TOML) | 6 | ~255,000 |
| Tests | 6 | ~800 |
| Web UI (HTML/CSS/JS) | 10 | ~1,500 |

---

## 11. Dependencies

### 11.1 Vendored (compiled into binary)

| Library | Version | License | Size | Purpose |
|---|---|---|---|---|
| [cJSON](https://github.com/DaveGamble/cJSON) | 1.7.x | MIT | ~2,000 lines | JSON parse/build |
| [SQLite](https://sqlite.org) | 3.45.x | Public domain | ~250 KB (amalgamation) | Memory, sessions, cron |
| [toml-c](https://github.com/cktan/tomlc99) | latest | MIT | ~500 lines | Config file parsing |

### 11.2 System libraries (linked)

| Library | Package | License | Purpose |
|---|---|---|---|
| [libcurl](https://curl.se/libcurl/) | `libcurl4-openssl-dev` | MIT | HTTPS calls to LLM APIs |
| [libmicrohttpd](https://www.gnu.org/software/libmicrohttpd/) | `libmicrohttpd-dev` | LGPL 2.1 | HTTP server |
| [libwebsockets](https://libwebsockets.org/) | `libwebsockets-dev` | MIT | WebSocket server |

### 11.3 Optional system libraries

| Library | Package | Purpose |
|---|---|---|
| [libgpiod](https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/) | `libgpiod-dev` | GPIO (modern alternative to sysfs) |
| [libcamera](https://libcamera.org/) | `libcamera-dev` | Camera capture (RPi camera module) |

### 11.4 Install on Raspberry Pi OS

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    libcurl4-openssl-dev \
    libmicrohttpd-dev \
    libwebsockets-dev \
    libgpiod-dev \
    libcamera-dev
```

---

## 12. Build System

### 12.1 Makefile (primary)

```makefile
CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -DNDEBUG
LDFLAGS  = -lcurl -lmicrohttpd -lwebsockets -lpthread -lm -ldl
SRC      = $(wildcard src/*.c src/**/*.c src/vendor/*.c)
OBJ      = $(SRC:.c=.o)
TARGET   = shellclaw

all: embed_ui $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

embed_ui:
	./scripts/embed_ui.sh

clean:
	rm -f $(OBJ) $(TARGET) src/gateway/ui_assets.h

install: $(TARGET)
	install -m 0755 $(TARGET) /usr/local/bin/shellclaw

test:
	$(CC) $(CFLAGS) -o test_runner tests/*.c src/**/*.c src/vendor/*.c $(LDFLAGS)
	./test_runner
```

### 12.2 Cross-compilation

For building on x86 desktop, deploying to ARM RPi:

```bash
# Install cross-compiler
sudo apt install gcc-aarch64-linux-gnu

# Cross-compile
CC=aarch64-linux-gnu-gcc make
```

### 12.3 Build profiles

| Profile | Flags | Binary size | Use |
|---|---|---|---|
| Debug | `-g -O0 -DDEBUG` | ~2 MB | Development |
| Release | `-O2 -DNDEBUG` | ~400 KB | Production |
| MinSize | `-Os -DNDEBUG -s` | ~300 KB | Constrained devices |

---

## 13. Testing Strategy

### 13.1 Unit tests

Each module has corresponding test file in `tests/`:

| Test file | Tests |
|---|---|
| `test_config.c` | TOML parsing, env var override, defaults |
| `test_memory.c` | SQLite save/recall, FTS5 search, session CRUD |
| `test_envelope.c` | ASAP envelope parse/build, ULID generation |
| `test_agent.c` | Context assembly, tool call parsing, compaction |
| `test_sandbox.c` | Namespace isolation, cgroup limits, blocked commands |
| `test_tools.c` | File path validation, cron parsing, GPIO mock |

### 13.2 Integration tests

```bash
# Start shellclaw in test mode
./shellclaw --config test.toml --test-mode &

# Run integration tests
./tests/integration/test_telegram_flow.sh
./tests/integration/test_asap_roundtrip.sh
./tests/integration/test_web_ui.sh
```

### 13.3 Hardware tests (on-device only)

```bash
# Run only on Raspberry Pi with hardware connected
SHELLCLAW_HW_TEST=1 ./test_runner

# Tests:
# - GPIO read/write on pin 17
# - I2C scan on bus 1
# - BME280 read (if connected)
# - Camera capture (if CSI camera connected)
```

### 13.4 ASAP compliance

Using the ASAP Protocol compliance harness:

```bash
# Start shellclaw with ASAP enabled
./shellclaw --config test.toml &

# Run ASAP compliance tests
pip install asap-compliance
pytest --asap-agent-url http://localhost:18789 -m asap_compliance
```

---

## 14. Roadmap

### Phase 1: Foundation (v0.1.0) — "The Loop"

**Goal:** Working agent that can chat via CLI and Telegram.

| Task | Module | Est. lines |
|---|---|---|
| Config parser (TOML) | `core/config.c` | 200 |
| Agent loop (ReAct) | `core/agent.c` | 300 |
| Memory store (SQLite) | `core/memory.c` | 250 |
| Anthropic provider | `providers/anthropic.c` | 200 |
| OpenAI provider | `providers/openai.c` | 200 |
| Provider router | `providers/router.c` | 100 |
| CLI channel | `channels/cli.c` | 50 |
| Telegram channel | `channels/telegram.c` | 200 |
| Shell tool | `tools/shell.c` | 100 |
| Web search tool | `tools/web_search.c` | 100 |
| File tool | `tools/file.c` | 100 |
| Skill loader (basic) | `core/skill.c` | 80 |
| Main + signal handling | `main.c` | 100 |

**Deliverable:** `shellclaw agent -m "Hello"` works. Telegram bot responds.

---

### Phase 2: Gateway (v0.2.0) — "The Interface"

**Goal:** HTTP gateway with embedded Web UI and ASAP manifest.

| Task | Module | Est. lines |
|---|---|---|
| HTTP server | `gateway/http.c` | 200 |
| WebSocket server | `gateway/ws.c` | 100 |
| Embedded UI server | `gateway/static.c` | 50 |
| Web UI (HTML/CSS/JS) | `web/*` | 1,500 |
| UI embed script | `scripts/embed_ui.sh` | 50 |
| Cron scheduler | `tools/cron.c` | 150 |
| Skill hot-reload (inotify) | `core/skill.c` | +70 |
| Heartbeat | integrated in agent | +50 |
| ASAP manifest endpoint | `asap/manifest.c` | 50 |

**Deliverable:** Web UI accessible at `http://shellclaw.local:18789`.
ASAP manifest at `/.well-known/asap/manifest.json`.

---

### Phase 3: Hardware (v0.3.0) — "The Body"

**Goal:** Agent interacts with physical world and runs sandboxed.

| Task | Module | Est. lines |
|---|---|---|
| GPIO tool | `tools/hardware.c` | 80 |
| I2C/sensor tool | `tools/hardware.c` | +80 |
| Camera tool | `tools/hardware.c` | +40 |
| Sandbox (namespaces) | `sandbox/sandbox.c` | 200 |
| Command allowlist | `sandbox/allowlist.c` | 100 |
| Discord channel | `channels/discord.c` | 250 |
| ASAP client | `asap/client.c` | 100 |
| ASAP registry | `asap/registry.c` | 80 |
| asap_invoke tool | `tools/asap_invoke.c` | 100 |

**Deliverable:** `gpio_read`, `sensor_read`, `camera_capture` work as tools.
Shell commands run in sandboxed namespaces. Agent can call external ASAP agents.

---

### Phase 4: Autonomy (v0.4.0) — "The Mind"

**Goal:** Agent operates autonomously with offline fallback.

| Task | Module | Est. lines |
|---|---|---|
| Local provider (llama.cpp) | `providers/local.c` | 100 |
| Provider fallback logic | `providers/router.c` | +50 |
| ASAP server (inbound) | `asap/server.c` | 100 |
| ASAP envelope handler | `asap/envelope.c` | 150 |
| ULID generator | `asap/ulid.c` | 50 |
| systemd service file | `systemd/shellclaw.service` | 20 |
| OTA update mechanism | `scripts/update.sh` | 50 |

**Deliverable:** Agent continues working when internet drops (local model).
Other ASAP agents can send tasks to ShellClaw. Runs as systemd service.

---

### Phase 5: Production (v1.0.0) — "The Release"

**Goal:** Production-ready with full security and documentation.

| Task | Description |
|---|---|
| Ed25519 manifest signing | Sign manifest with ASAP trust levels |
| ASAP marketplace registration | Register ShellClaw in public registry |
| Security audit | Review sandbox, auth, allowlists |
| Performance benchmarking | Measure latency, memory, CPU on RPi Zero 2 W |
| Pre-built RPi image | Flash-and-use SD card image |
| Full documentation | README, ARCHITECTURE, HARDWARE, SECURITY, ASAP guides |
| ASAP compliance passing | All compliance harness tests green |

---

## 15. Comparison with Existing Solutions

| Feature | OpenClaw | ZeroClaw | PicoClaw | MimiClaw | SubZeroClaw | **ShellClaw** |
|---|---|---|---|---|---|---|
| **Language** | TypeScript | Rust | Go | C | C | **C** |
| **Lines of code** | ~430,000 | ~15,000 | ~8,000 | ~5,000 | ~380 | **~4,000** |
| **Binary size** | ~28 MB | ~8.8 MB | ~8 MB | firmware | 54 KB | **~400 KB** |
| **RAM (runtime)** | >1 GB | <5 MB | <10 MB | ~2 MB | ~2 MB | **<5 MB** |
| **Startup time** | >500s (0.8 GHz) | <10 ms | <1s | N/A | <1s | **<1s** |
| **Target hardware** | Mac Mini | Any ($10) | LicheeRV ($10) | ESP32 ($5) | Any Linux | **RPi Zero ($15)** |
| **Runs Linux** | Yes (Node.js) | Yes | Yes | No | Yes | **Yes** |
| **Process sandbox** | No | Docker | No | No | No | **cgroups native** |
| **Hardware access** | No | USB basic | No | ESP32 GPIO | No | **GPIO/I2C/Camera** |
| **Web UI** | Heavy | None | None | None | None | **Embedded** |
| **Offline mode** | No | No | No | No | No | **llama.cpp** |
| **Agent-to-agent** | No | No | No | No | No | **ASAP Protocol** |
| **Hot-reload skills** | No | No | No | No | No | **inotify()** |
| **Multi-provider** | Yes | Yes | Yes | Yes | No | **Yes + fallback** |
| **Channels** | 20+ | 17+ | 7+ | 1 (Telegram) | 0 (CLI) | **4 (CLI/TG/DC/WS)** |

### Key differentiators

1. **Only C agent with Linux capabilities.** MimiClaw is C but bare-metal
   (no shell, no SQLite, no process isolation). SubZeroClaw is C on Linux but
   has no architecture (single file, no modularity). ShellClaw combines C
   efficiency with Linux richness.

2. **Only agent with native sandbox.** ZeroClaw uses Docker (500ms+ startup,
   daemon required). ShellClaw uses `clone()` + cgroups (<1ms, zero daemon).

3. **Only agent with hardware tools.** No other Linux-based agent exposes GPIO,
   I2C sensors, or camera as LLM-callable tools.

4. **Only agent with ASAP Protocol.** No other agent in the ecosystem can
   participate in standardized agent-to-agent communication.

5. **Only agent with offline fallback.** When internet connectivity is lost,
   ShellClaw continues operating with a local model.

6. **Only agent with embedded Web UI.** Self-contained config and monitoring
   interface compiled into the binary, zero external dependencies.

---

## 16. Open Questions

| # | Question | Options | Leaning |
|---|---|---|---|
| 1 | Project name | ShellClaw, CrabClaw, PiClaw, EdgeClaw | ShellClaw (reflects shell-as-tool philosophy) |
| 2 | License | MIT, Apache-2.0, MIT + Apache-2.0 | MIT (simplicity, matches MimiClaw/SubZeroClaw) |
| 3 | GPIO library | sysfs (`/sys/class/gpio/`), libgpiod | libgpiod (modern, sysfs is deprecated) |
| 4 | HTTP server | libmicrohttpd, mongoose, custom | libmicrohttpd (battle-tested, minimal) |
| 5 | WebSocket | libwebsockets, custom over TCP | libwebsockets (full RFC 6455 compliance) |
| 6 | TOML parser | tomlc99, custom | tomlc99 (MIT, small, well-tested) |
| 7 | Multimodal input | Base64 images in API call, upload to S3 | Base64 (simpler, works for small images) |
| 8 | Tailscale integration | Built-in, external script | External script (keep core simple) |
| 9 | Minimum RPi OS | Lite (headless), Full | Lite (smaller, headless is the use case) |
| 10 | ASAP version target | v2.1 (current), track latest | v2.1 (stable, well-documented) |

---

## 17. References

### Projects analyzed

| Project | URL | Key insight adopted |
|---|---|---|
| OpenClaw | [github.com/openclaw/openclaw](https://github.com/openclaw/openclaw) | Gateway concept, WebSocket protocol, skills platform |
| ZeroClaw | [github.com/zeroclaw-labs/zeroclaw](https://github.com/zeroclaw-labs/zeroclaw) | Trait architecture, security checklist, provider catalog |
| PicoClaw | [github.com/sipeed/picoclaw](https://github.com/sipeed/picoclaw) | model_list config, channel router, cron scheduler |
| MimiClaw | [github.com/memovai/mimiclaw](https://github.com/memovai/mimiclaw) | Proof C works, multi-provider, SPIFFS/flash memory |
| SubZeroClaw | [github.com/jmlago/subzeroclaw](https://github.com/jmlago/subzeroclaw) | Shell-as-tool, skills-as-markdown, radical simplicity |
| NanoClaw | [github.com/qwibitai/nanoclaw](https://github.com/qwibitai/nanoclaw) | Container isolation philosophy |
| TinyClaw | [github.com/TinyAGI/tinyclaw](https://github.com/TinyAGI/tinyclaw) | Multi-agent via IPC, team collaboration |

### Protocol

| Resource | URL |
|---|---|
| ASAP Protocol | [github.com/adriannoes/asap-protocol](https://github.com/adriannoes/asap-protocol) |
| ASAP Documentation | [asap-protocol docs](https://github.com/adriannoes/asap-protocol/tree/main/docs) |
| ASAP PyPI | [pypi.org/project/asap-protocol](https://pypi.org/project/asap-protocol/) |

### Hardware

| Resource | URL |
|---|---|
| Raspberry Pi Zero 2 W | [raspberrypi.com/products/raspberry-pi-zero-2-w](https://www.raspberrypi.com/products/raspberry-pi-zero-2-w/) |
| libgpiod | [git.kernel.org/.../libgpiod](https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/) |
| libcamera | [libcamera.org](https://libcamera.org/) |
| llama.cpp | [github.com/ggml-org/llama.cpp](https://github.com/ggml-org/llama.cpp) |

### Vendored libraries

| Library | URL | License |
|---|---|---|
| cJSON | [github.com/DaveGamble/cJSON](https://github.com/DaveGamble/cJSON) | MIT |
| SQLite | [sqlite.org](https://sqlite.org/) | Public domain |
| tomlc99 | [github.com/cktan/tomlc99](https://github.com/cktan/tomlc99) | MIT |

---

*This document is a living plan. It will be updated as implementation
progresses and architectural decisions are validated.*
