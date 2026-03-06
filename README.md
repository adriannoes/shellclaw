# ShellClaw

**The first physical AI agent that participates in a global agent ecosystem.**

A lightweight AI assistant written in C, designed to run on a Raspberry Pi
Zero 2 W (~$15) and communicate with other agents through the
[ASAP Protocol](https://github.com/adriannoes/asap-protocol) and the
[agentic marketplace](https://asap-protocol.vercel.app/).

> **Status:** Planning phase. See [PLAN.md](PLAN.md) for the full architecture
> and implementation plan.

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

MIT
