# ShellClaw Phase 1 — debug/release profiles, C11, libcurl for providers
CC     ?= gcc
BUILD  ?= debug
BINDIR ?= build
DSYMDIR ?= tests-dSYM
INC    := -I. -I tests -I src -I vendor/tomlc99 -I vendor/sqlite3 -I vendor/cJSON
LDLIBS := -lcurl -lm
# Gateway (Phase 2): libwebsockets for HTTP+WebSocket on same port
# Install: brew install libwebsockets
# Set GATEWAY=0 to build without gateway when libwebsockets not installed
GATEWAY ?= $(shell pkg-config --exists libwebsockets 2>/dev/null && echo 1 || echo 0)
GATEWAY_CFLAGS := $(if $(filter 1,$(GATEWAY)),$(shell pkg-config --cflags libwebsockets 2>/dev/null),)
GATEWAY_CFLAGS += $(if $(filter 1,$(GATEWAY)),$(shell pkg-config --cflags openssl 2>/dev/null),)
GATEWAY_LDLIBS := $(if $(filter 1,$(GATEWAY)),$(shell pkg-config --libs libwebsockets 2>/dev/null),)
GATEWAY_LDLIBS += $(if $(filter 1,$(GATEWAY)),$(shell pkg-config --libs openssl 2>/dev/null),)
LDFLAGS :=
# On macOS debug builds: generate .dSYM into tests-dSYM/ and remove any from BINDIR
DSYM_SCRIPT = @mkdir -p $(DSYMDIR) && ( [ "$$(uname)" != "Darwin" ] || [ "$(BUILD)" != "debug" ] || dsymutil $(BINDIR)/$@ -o $(DSYMDIR)/$@.dSYM 2>/dev/null ); rm -rf $(BINDIR)/$@.dSYM

ifeq ($(BUILD),release)
CFLAGS ?= -std=c11 -Wall -Wextra -Os -DNDEBUG -ffunction-sections -fdata-sections
# macOS ld uses -dead_strip; GNU ld uses --gc-sections
ifeq ($(shell uname 2>/dev/null),Darwin)
LDFLAGS += -Wl,-dead_strip
else
LDFLAGS += -Wl,--gc-sections
endif
else ifeq ($(BUILD),coverage)
CFLAGS ?= -std=c11 -Wall -Wextra -g -O0 -DDEBUG --coverage
LDFLAGS := --coverage
else
CFLAGS ?= -std=c11 -Wall -Wextra -g -O0 -DDEBUG
endif
CFLAGS += -Wformat=2 -Wformat-security
CFLAGS += $(if $(filter 1,$(GATEWAY)),-DSHELLCLAW_GATEWAY,)
ifeq ($(CI),true)
CFLAGS += -Werror
endif
# Vendor code (toml, sqlite3, cJSON) may emit warnings with GCC on Linux; exclude -Werror
VENDOR_CFLAGS := $(filter-out -Werror,$(CFLAGS))

# Core
CONFIG_O  := src/core/config.o
MAIN_O    := src/core/main.o
MEMORY_O  := src/core/memory.o
SKILL_O   := src/core/skill.o
AGENT_O   := src/core/agent.o
DAEMON_O  := src/core/daemon.o
RELOAD_O  := src/core/reload.o
BOOTSTRAP_O := src/core/bootstrap.o
DISPATCH_O := src/core/dispatch.o
# Vendor
TOML_O   := vendor/tomlc99/toml.o
SQLITE3_O := vendor/sqlite3/sqlite3.o
# Providers (Task 4)
PROVIDER_COMMON_O := src/providers/provider_common.o
STUB_O            := src/providers/stub.o
CJSON_O     := vendor/cJSON/cJSON.o
ANTHROPIC_O := src/providers/anthropic.o
OPENAI_O   := src/providers/openai.o
OPENAI_COMPAT_O := src/providers/openai_compat.o
LOCAL_O    := src/providers/local.o
ROUTER_O   := src/providers/router.o
# Channels (Task 6)
CHANNEL_COMMON_O := src/channels/channel_common.o
CHANNEL_STUB_O   := src/channels/stub.o
CHANNEL_CLI_O    := src/channels/cli.o
CHANNEL_TG_O     := src/channels/telegram.o
CHANNEL_DISCORD_O := src/channels/discord.o
DISCORD_HELPERS_O := src/channels/discord_helpers.o
CHANNEL_HEARTBEAT_O := src/channels/heartbeat.o
# Gateway (Phase 2) - auth always built (no libwebsockets); http/ws when libwebsockets available
AUTH_O   := src/gateway/auth.o
CHANNEL_WEBCHAT_O := $(if $(filter 1,$(GATEWAY)),src/channels/webchat.o,)
STATIC_O := $(if $(filter 1,$(GATEWAY)),src/gateway/static.o,)
HTTP_O     := $(if $(filter 1,$(GATEWAY)),src/gateway/http.o,)
HTTP_LWS_O := $(if $(filter 1,$(GATEWAY)),src/gateway/http_lws.o,)
ROUTES_O   := $(if $(filter 1,$(GATEWAY)),src/gateway/routes.o,)
WS_O       := $(if $(filter 1,$(GATEWAY)),src/gateway/ws.o,)
# Tools (Task 7)
SHELL_O    := src/tools/shell.o
WEBSEARCH_O := src/tools/web_search.o
FILE_O     := src/tools/file.o
REGISTRY_O := src/tools/registry.o
CONTEXT_O  := src/tools/context.o
CONTEXT_CACHE_O := src/tools/context_cache.o
CONTEXT_HTTP_O := src/tools/context_http.o
CONTEXT_GEO_O := src/tools/context_geo.o
CRYPTO_O := src/crypto/crypto.o
HARDWARE_STUB_O := src/hardware/hardware_stub.o
CRON_O         := src/tools/cron.o
ASAP_INVOKE_O  := src/tools/asap_invoke.o
ASAP_INVOKE_TEST_O := $(BINDIR)/asap_invoke_test.o
# Sandbox (Phase 3 §5)
SANDBOX_O  := src/sandbox/sandbox.o
ALLOWLIST_O := src/sandbox/allowlist.o
MANIFEST_O := src/asap/manifest.o
ENVELOPE_O := src/asap/envelope.o
ULID_O := src/asap/ulid.o
CLIENT_O := src/asap/client.o
ASAP_REGISTRY_O := src/asap/registry.o
SERVER_O := src/asap/server.o
ASAP_LOG_O := src/asap/log.o
RATE_LIMIT_O := $(if $(filter 1,$(GATEWAY)),src/gateway/rate_limit.o,)
ASAP_REGISTRY_TEST_O := $(BINDIR)/asap_registry_test.o
# Phase 3 ASAP unit binaries: keep in sync with `test`, `coverage`, and scripts/coverage.sh TESTS list.
ASAP_UNIT_TESTS := test_asap_envelope test_asap_ulid test_asap_client test_asap_registry test_asap_server test_asap_invoke test_asap_log
# Provider objects built with SHELLCLAW_TEST for negative/parse tests (CR-21)
ANTHROPIC_TEST_O := $(BINDIR)/anthropic_test.o
OPENAI_TEST_O    := $(BINDIR)/openai_test.o
LOCAL_TEST_O     := $(BINDIR)/local_test.o
CHANNEL_TG_TEST_O := $(BINDIR)/telegram_test.o
CONTEXT_TEST_O := $(BINDIR)/context_test.o
CONTEXT_HTTP_TEST_O := $(BINDIR)/context_http_test.o
CONTEXT_GEO_TEST_O := $(BINDIR)/context_geo_test.o
CONTEXT_CACHE_TEST_O := $(BINDIR)/context_cache_test.o
HEARTBEAT_TEST_O := $(BINDIR)/heartbeat_test.o
CONTEXT_TEST_OBJS := $(CONTEXT_TEST_O) $(CONTEXT_HTTP_TEST_O) $(CONTEXT_GEO_TEST_O) $(CONTEXT_CACHE_TEST_O)
CORE_OBJS := $(CONFIG_O) $(MAIN_O) $(MEMORY_O) $(SKILL_O) $(AGENT_O) $(DAEMON_O) $(RELOAD_O) $(BOOTSTRAP_O) $(DISPATCH_O)
VENDOR_OBJS := $(TOML_O) $(SQLITE3_O) $(CJSON_O)
OBJS := $(CORE_OBJS) $(VENDOR_OBJS)
PROVIDER_OBJS := $(PROVIDER_COMMON_O) $(STUB_O) $(ROUTER_O) $(ANTHROPIC_O) $(OPENAI_COMPAT_O) $(OPENAI_O) $(LOCAL_O)
CHANNEL_OBJS := $(CHANNEL_COMMON_O) $(CHANNEL_CLI_O) $(CHANNEL_TG_O) $(CHANNEL_DISCORD_O) $(DISCORD_HELPERS_O) $(CHANNEL_HEARTBEAT_O) $(CHANNEL_WEBCHAT_O)
GATEWAY_OBJS := $(AUTH_O) $(STATIC_O) $(HTTP_O) $(HTTP_LWS_O) $(ROUTES_O) $(WS_O) $(RATE_LIMIT_O)
TOOL_OBJS := $(SHELL_O) $(WEBSEARCH_O) $(FILE_O) $(REGISTRY_O) $(CONTEXT_O) $(CONTEXT_CACHE_O) $(CONTEXT_HTTP_O) $(CONTEXT_GEO_O) $(CRYPTO_O) $(HARDWARE_STUB_O) $(CRON_O) $(ASAP_INVOKE_O)
ASAP_OBJS := $(MANIFEST_O) $(ENVELOPE_O) $(ULID_O) $(CLIENT_O) $(ASAP_REGISTRY_O) $(SERVER_O) $(ASAP_LOG_O)
SANDBOX_OBJS := $(SANDBOX_O) $(ALLOWLIST_O)
SHELLCLAW_OBJS := $(OBJS) $(PROVIDER_OBJS) $(CHANNEL_OBJS) $(GATEWAY_OBJS) $(ASAP_OBJS) $(TOOL_OBJS) $(SANDBOX_OBJS)
SQLITE_CFLAGS := -DSQLITE_ENABLE_FTS5

.PHONY: all debug release clean clean-root-dsym test shellclaw static coverage

all: debug

debug:
	$(MAKE) BUILD=debug shellclaw

release:
	$(MAKE) BUILD=release shellclaw

shellclaw: $(SHELLCLAW_OBJS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(BINDIR)/$@ $(SHELLCLAW_OBJS) $(LDLIBS) $(GATEWAY_LDLIBS) -pthread
	$(DSYM_SCRIPT)
	@if [ "$(BUILD)" = "release" ]; then strip -s $(BINDIR)/$@ 2>/dev/null || true; fi

$(CONFIG_O): src/core/config.c src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<

$(MAIN_O): src/core/main.c src/core/config.h src/core/bootstrap.h src/core/daemon.h src/core/dispatch.h src/core/reload.h src/channels/channel.h src/providers/provider.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/core/main.c

$(DAEMON_O): src/core/daemon.c src/core/daemon.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/core/daemon.c

$(RELOAD_O): src/core/reload.c src/core/reload.h src/core/bootstrap.h src/core/config.h src/channels/channel.h src/channels/heartbeat.h src/providers/provider.h src/tools/tool.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/core/reload.c

$(BOOTSTRAP_O): src/core/bootstrap.c src/core/bootstrap.h src/core/config.h src/core/memory.h src/core/skill.h src/channels/channel.h src/channels/heartbeat.h src/providers/provider.h src/tools/tool.h src/tools/cron.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/core/bootstrap.c

$(DISPATCH_O): src/core/dispatch.c src/core/dispatch.h src/core/agent.h src/core/bootstrap.h src/core/memory.h src/channels/channel.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/core/dispatch.c

$(TOML_O): vendor/tomlc99/toml.c vendor/tomlc99/toml.h
	$(CC) $(VENDOR_CFLAGS) $(INC) -c -o $@ $<

$(SQLITE3_O): vendor/sqlite3/sqlite3.c vendor/sqlite3/sqlite3.h
	$(CC) $(VENDOR_CFLAGS) $(SQLITE_CFLAGS) $(INC) -c -o $@ $<

$(MEMORY_O): src/core/memory.c src/core/memory.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<

$(SKILL_O): src/core/skill.c src/core/skill.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/core/skill.c

$(AGENT_O): src/core/agent.c src/core/agent.h src/core/config.h src/core/memory.h src/core/skill.h src/providers/provider.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/core/agent.c

$(PROVIDER_COMMON_O): src/providers/provider_common.c src/providers/provider.h vendor/cJSON/cJSON.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/providers/provider_common.c

$(STUB_O): src/providers/stub.c src/providers/provider.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/providers/stub.c

$(CJSON_O): vendor/cJSON/cJSON.c vendor/cJSON/cJSON.h
	$(CC) $(VENDOR_CFLAGS) $(INC) -c -o $@ vendor/cJSON/cJSON.c

$(ANTHROPIC_O): src/providers/anthropic.c src/providers/provider.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/providers/anthropic.c

$(ANTHROPIC_TEST_O): src/providers/anthropic.c src/providers/provider.h src/core/config.h
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(INC) -DSHELLCLAW_TEST -c -o $@ src/providers/anthropic.c

$(OPENAI_COMPAT_O): src/providers/openai_compat.c src/providers/openai_compat.h src/providers/provider.h vendor/cJSON/cJSON.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/providers/openai_compat.c

$(OPENAI_O): src/providers/openai.c src/providers/openai_compat.h src/providers/provider.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/providers/openai.c

$(OPENAI_TEST_O): src/providers/openai.c src/providers/openai_compat.h src/providers/provider.h src/core/config.h
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(INC) -DSHELLCLAW_TEST -c -o $@ src/providers/openai.c

$(LOCAL_O): src/providers/local.c src/providers/openai_compat.h src/providers/provider.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/providers/local.c

$(LOCAL_TEST_O): src/providers/local.c src/providers/openai_compat.h src/providers/provider.h src/core/config.h
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(INC) -DSHELLCLAW_TEST -c -o $@ src/providers/local.c

$(ROUTER_O): src/providers/router.c src/providers/provider.h src/core/config.h vendor/cJSON/cJSON.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/providers/router.c

$(CHANNEL_COMMON_O): src/channels/channel_common.c src/channels/channel.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/channels/channel_common.c

$(CHANNEL_STUB_O): src/channels/stub.c src/channels/channel.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/channels/stub.c

$(CHANNEL_CLI_O): src/channels/cli.c src/channels/channel.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/channels/cli.c

$(CHANNEL_TG_O): src/channels/telegram.c src/channels/channel.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/channels/telegram.c

$(CHANNEL_DISCORD_O): src/channels/discord.c src/channels/channel.h src/channels/discord_helpers.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) $(GATEWAY_CFLAGS) -c -o $@ src/channels/discord.c

$(DISCORD_HELPERS_O): src/channels/discord_helpers.c src/channels/discord_helpers.h src/channels/channel.h vendor/cJSON/cJSON.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/channels/discord_helpers.c

$(CHANNEL_HEARTBEAT_O): src/channels/heartbeat.c src/channels/heartbeat.h src/channels/channel.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/channels/heartbeat.c

$(HEARTBEAT_TEST_O): src/channels/heartbeat.c src/channels/heartbeat.h src/channels/channel.h src/core/config.h
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(INC) -DSHELLCLAW_TEST -c -o $@ src/channels/heartbeat.c

$(CHANNEL_WEBCHAT_O): src/channels/webchat.c src/channels/channel.h src/channels/webchat.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) $(GATEWAY_CFLAGS) -c -o $@ src/channels/webchat.c

$(AUTH_O): src/gateway/auth.c src/gateway/auth.h src/core/config.h src/crypto/crypto.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/gateway/auth.c

WS_TEST_O := src/gateway/ws_test.o
$(WS_TEST_O): src/gateway/ws.c src/gateway/ws.h
	$(CC) $(CFLAGS) $(INC) -DSHELLCLAW_WS_TEST -pthread -c -o $@ src/gateway/ws.c

src/gateway/ui_assets.h: web/index.html web/css/style.css web/js/app.js scripts/embed_ui.sh
	@mkdir -p src/gateway
	@chmod +x scripts/embed_ui.sh
	./scripts/embed_ui.sh

src/gateway/static.o: src/gateway/static.c src/gateway/static.h src/gateway/ui_assets.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/gateway/static.c

$(MANIFEST_O): src/asap/manifest.c src/asap/manifest.h src/asap/asap_version.h src/core/config.h src/core/skill.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/asap/manifest.c

$(ENVELOPE_O): src/asap/envelope.c src/asap/envelope.h src/asap/asap_version.h vendor/cJSON/cJSON.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/asap/envelope.c

$(ULID_O): src/asap/ulid.c src/asap/ulid.h src/crypto/crypto.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/asap/ulid.c

$(CLIENT_O): src/asap/client.c src/asap/client.h src/asap/envelope.h src/asap/ulid.h src/core/config.h src/providers/provider.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/asap/client.c

$(ASAP_REGISTRY_O): src/asap/registry.c src/asap/registry.h src/asap/client.h src/providers/provider.h vendor/cJSON/cJSON.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/asap/registry.c

$(SERVER_O): src/asap/server.c src/asap/server.h src/asap/envelope.h src/asap/asap_version.h src/asap/ulid.h src/core/agent.h src/core/config.h src/core/memory.h src/providers/provider.h vendor/cJSON/cJSON.h
	$(CC) $(CFLAGS) $(INC) -pthread -c -o $@ src/asap/server.c

$(ASAP_LOG_O): src/asap/log.c src/asap/log.h
	$(CC) $(CFLAGS) $(INC) -pthread -c -o $@ src/asap/log.c

$(ASAP_REGISTRY_TEST_O): src/asap/registry.c src/asap/registry.h src/asap/client.h src/providers/provider.h vendor/cJSON/cJSON.h
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(INC) -DSHELLCLAW_REGISTRY_TEST -c -o $@ src/asap/registry.c

$(RATE_LIMIT_O): src/gateway/rate_limit.c src/gateway/rate_limit.h
	$(CC) $(CFLAGS) $(INC) -pthread -c -o $@ src/gateway/rate_limit.c

$(HTTP_O): src/gateway/http.c src/gateway/http.h src/gateway/http_lws.h src/gateway/ws.h src/providers/provider.h
	$(CC) $(CFLAGS) $(INC) $(GATEWAY_CFLAGS) -pthread -c -o $@ src/gateway/http.c

$(HTTP_LWS_O): src/gateway/http_lws.c src/gateway/http_lws.h src/gateway/routes.h src/gateway/auth.h src/gateway/static.h src/gateway/ws.h
	$(CC) $(CFLAGS) $(INC) $(GATEWAY_CFLAGS) -pthread -c -o $@ src/gateway/http_lws.c

$(ROUTES_O): src/gateway/routes.c src/gateway/routes.h src/gateway/http_lws.h src/gateway/auth.h src/gateway/rate_limit.h src/tools/context.h src/asap/manifest.h src/asap/envelope.h src/asap/server.h src/asap/log.h src/core/config.h src/core/memory.h src/core/skill.h src/providers/provider.h src/channels/channel.h src/tools/cron.h
	$(CC) $(CFLAGS) $(INC) $(GATEWAY_CFLAGS) -pthread -c -o $@ src/gateway/routes.c

$(WS_O): src/gateway/ws.c src/gateway/ws.h
	$(CC) $(CFLAGS) $(INC) $(GATEWAY_CFLAGS) -c -o $@ src/gateway/ws.c

$(SHELL_O): src/tools/shell.c src/tools/tool.h src/tools/shell.h src/core/config.h \
            src/sandbox/sandbox.h src/sandbox/allowlist.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/tools/shell.c

$(SANDBOX_O): src/sandbox/sandbox.c src/sandbox/sandbox.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/sandbox/sandbox.c

$(ALLOWLIST_O): src/sandbox/allowlist.c src/sandbox/allowlist.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/sandbox/allowlist.c

$(WEBSEARCH_O): src/tools/web_search.c src/tools/tool.h src/tools/web_search.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/tools/web_search.c

$(FILE_O): src/tools/file.c src/tools/tool.h src/tools/file.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/tools/file.c

$(REGISTRY_O): src/tools/registry.c src/tools/tool.h src/tools/shell.h src/tools/web_search.h src/tools/file.h src/tools/context.h src/tools/asap_invoke.h src/hardware/hardware.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/tools/registry.c

$(CRYPTO_O): src/crypto/crypto.c src/crypto/crypto.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/crypto/crypto.c

$(CONTEXT_O): src/tools/context.c src/tools/context.h src/tools/context_internal.h src/tools/tool.h src/core/config.h vendor/cJSON/cJSON.h
	$(CC) $(CFLAGS) $(INC) -pthread -c -o $@ src/tools/context.c

$(CONTEXT_CACHE_O): src/tools/context_cache.c src/tools/context_internal.h src/core/config.h vendor/cJSON/cJSON.h
	$(CC) $(CFLAGS) $(INC) -pthread -c -o $@ src/tools/context_cache.c

$(CONTEXT_HTTP_O): src/tools/context_http.c src/tools/context_internal.h
	$(CC) $(CFLAGS) $(INC) -pthread -c -o $@ src/tools/context_http.c

$(CONTEXT_GEO_O): src/tools/context_geo.c src/tools/context_internal.h src/core/config.h vendor/cJSON/cJSON.h
	$(CC) $(CFLAGS) $(INC) -pthread -c -o $@ src/tools/context_geo.c

$(CONTEXT_TEST_O): src/tools/context.c src/tools/context.h src/tools/context_internal.h src/tools/tool.h src/core/config.h vendor/cJSON/cJSON.h
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(INC) -DSHELLCLAW_CONTEXT_TEST -pthread -c -o $@ src/tools/context.c

$(CONTEXT_HTTP_TEST_O): src/tools/context_http.c src/tools/context_internal.h
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(INC) -DSHELLCLAW_CONTEXT_TEST -pthread -c -o $@ src/tools/context_http.c

$(CONTEXT_GEO_TEST_O): src/tools/context_geo.c src/tools/context_internal.h src/core/config.h vendor/cJSON/cJSON.h
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(INC) -DSHELLCLAW_CONTEXT_TEST -pthread -c -o $@ src/tools/context_geo.c

$(CONTEXT_CACHE_TEST_O): src/tools/context_cache.c src/tools/context_internal.h src/core/config.h vendor/cJSON/cJSON.h
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(INC) -DSHELLCLAW_CONTEXT_TEST -pthread -c -o $@ src/tools/context_cache.c

$(HARDWARE_STUB_O): src/hardware/hardware_stub.c src/hardware/hardware.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/hardware/hardware_stub.c

$(CRON_O): src/tools/cron.c src/tools/cron.h src/crypto/crypto.h src/core/memory.h src/channels/channel.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/tools/cron.c

$(ASAP_INVOKE_O): src/tools/asap_invoke.c src/tools/asap_invoke.h src/tools/tool.h src/asap/envelope.h src/asap/client.h src/asap/registry.h src/asap/ulid.h src/asap/asap_version.h src/core/config.h vendor/cJSON/cJSON.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/tools/asap_invoke.c

$(ASAP_INVOKE_TEST_O): src/tools/asap_invoke.c src/tools/asap_invoke.h src/tools/tool.h src/asap/envelope.h src/asap/client.h src/asap/registry.h src/asap/ulid.h src/asap/asap_version.h src/core/config.h vendor/cJSON/cJSON.h
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(INC) -DSHELLCLAW_ASAP_INVOKE_TEST -DSHELLCLAW_REGISTRY_TEST -c -o $@ src/tools/asap_invoke.c

test_config: tests/test_config.c $(CONFIG_O) $(TOML_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_config.c $(CONFIG_O) $(TOML_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_memory: tests/test_memory.c $(MEMORY_O) $(SQLITE3_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_memory.c $(MEMORY_O) $(SQLITE3_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_skill: tests/test_skill.c $(SKILL_O) $(CONFIG_O) $(TOML_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_skill.c $(SKILL_O) $(CONFIG_O) $(TOML_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_provider: tests/test_provider.c $(STUB_O) $(PROVIDER_COMMON_O) $(CJSON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_provider.c $(STUB_O) $(PROVIDER_COMMON_O) $(CJSON_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_anthropic: tests/test_anthropic.c $(ANTHROPIC_TEST_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(CJSON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -DSHELLCLAW_TEST -o $(BINDIR)/$@ tests/test_anthropic.c $(ANTHROPIC_TEST_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(CJSON_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_openai: tests/test_openai.c $(OPENAI_TEST_O) $(OPENAI_COMPAT_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(CJSON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -DSHELLCLAW_TEST -o $(BINDIR)/$@ tests/test_openai.c $(OPENAI_TEST_O) $(OPENAI_COMPAT_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(CJSON_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_local_provider: tests/test_local_provider.c $(LOCAL_TEST_O) $(OPENAI_COMPAT_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(CJSON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -DSHELLCLAW_TEST -o $(BINDIR)/$@ tests/test_local_provider.c $(LOCAL_TEST_O) $(OPENAI_COMPAT_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(CJSON_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_router: tests/test_router.c $(ROUTER_O) $(STUB_O) $(ANTHROPIC_O) $(OPENAI_COMPAT_O) $(OPENAI_O) $(LOCAL_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(CJSON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_router.c $(ROUTER_O) $(STUB_O) $(ANTHROPIC_O) $(OPENAI_COMPAT_O) $(OPENAI_O) $(LOCAL_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(CJSON_O) $(LDLIBS) -pthread
	$(DSYM_SCRIPT)

test_heartbeat: tests/test_heartbeat.c $(HEARTBEAT_TEST_O) $(CHANNEL_COMMON_O) $(CONFIG_O) $(TOML_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -DSHELLCLAW_TEST -o $(BINDIR)/$@ tests/test_heartbeat.c $(HEARTBEAT_TEST_O) $(CHANNEL_COMMON_O) $(CONFIG_O) $(TOML_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_crypto: tests/test_crypto.c $(CRYPTO_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_crypto.c $(CRYPTO_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_hardware_stub: tests/test_hardware_stub.c $(HARDWARE_STUB_O) $(CONFIG_O) $(TOML_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_hardware_stub.c $(HARDWARE_STUB_O) $(CONFIG_O) $(TOML_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_agent: tests/test_agent.c $(AGENT_O) $(ROUTER_O) $(STUB_O) $(ANTHROPIC_O) $(OPENAI_COMPAT_O) $(OPENAI_O) $(LOCAL_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(MEMORY_O) $(SKILL_O) $(SQLITE3_O) $(CJSON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_agent.c $(AGENT_O) $(ROUTER_O) $(STUB_O) $(ANTHROPIC_O) $(OPENAI_COMPAT_O) $(OPENAI_O) $(LOCAL_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(MEMORY_O) $(SKILL_O) $(SQLITE3_O) $(CJSON_O) $(LDLIBS) -pthread
	$(DSYM_SCRIPT)

test_channel: tests/test_channel.c $(CHANNEL_COMMON_O) $(CHANNEL_STUB_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_channel.c $(CHANNEL_COMMON_O) $(CHANNEL_STUB_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_cli: tests/test_cli.c $(CHANNEL_COMMON_O) $(CHANNEL_CLI_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_cli.c $(CHANNEL_COMMON_O) $(CHANNEL_CLI_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_shell: tests/test_shell.c $(SHELL_O) $(SANDBOX_O) $(ALLOWLIST_O) $(CONFIG_O) $(TOML_O) $(CJSON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_shell.c $(SHELL_O) $(SANDBOX_O) $(ALLOWLIST_O) $(CONFIG_O) $(TOML_O) $(CJSON_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_file: tests/test_file.c $(FILE_O) $(REGISTRY_O) $(CONFIG_O) $(TOML_O) $(CJSON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_file.c $(FILE_O) $(CONFIG_O) $(TOML_O) $(CJSON_O) $(LDLIBS)
	$(DSYM_SCRIPT)

$(CHANNEL_TG_TEST_O): src/channels/telegram.c src/channels/channel.h src/core/config.h
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(INC) -DSHELLCLAW_TEST -c -o $@ src/channels/telegram.c

test_telegram: tests/test_telegram.c $(CHANNEL_TG_TEST_O) $(CHANNEL_COMMON_O) $(CONFIG_O) $(TOML_O) $(CJSON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -DSHELLCLAW_TEST -o $(BINDIR)/$@ tests/test_telegram.c $(CHANNEL_TG_TEST_O) $(CHANNEL_COMMON_O) $(CONFIG_O) $(TOML_O) $(CJSON_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_discord_helpers: tests/test_discord_helpers.c $(DISCORD_HELPERS_O) $(CJSON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_discord_helpers.c $(DISCORD_HELPERS_O) $(CJSON_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_web_search: tests/test_web_search.c $(WEBSEARCH_O) $(CONFIG_O) $(TOML_O) $(CJSON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_web_search.c $(WEBSEARCH_O) $(CONFIG_O) $(TOML_O) $(CJSON_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_cron: tests/test_cron.c $(CRON_O) $(CRYPTO_O) $(MEMORY_O) $(SQLITE3_O) $(CHANNEL_COMMON_O) $(CJSON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_cron.c $(CRON_O) $(CRYPTO_O) $(MEMORY_O) $(SQLITE3_O) $(CHANNEL_COMMON_O) $(CJSON_O) $(LDLIBS)

test_ws: tests/test_ws.c $(WS_TEST_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_ws.c $(WS_TEST_O) $(LDLIBS) -pthread
	$(DSYM_SCRIPT)
	$(DSYM_SCRIPT)

test_context: tests/test_context.c $(CONTEXT_TEST_OBJS) $(CONFIG_O) $(TOML_O) $(CJSON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -DSHELLCLAW_CONTEXT_TEST -o $(BINDIR)/$@ tests/test_context.c $(CONTEXT_TEST_OBJS) $(CONFIG_O) $(TOML_O) $(CJSON_O) $(LDLIBS) -pthread
	$(DSYM_SCRIPT)

test_auth: tests/test_auth.c $(AUTH_O) $(CRYPTO_O) $(CJSON_O) $(CONFIG_O) $(TOML_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_auth.c $(AUTH_O) $(CRYPTO_O) $(CJSON_O) $(CONFIG_O) $(TOML_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_manifest: tests/test_manifest.c $(MANIFEST_O) $(CONFIG_O) $(TOML_O) $(SKILL_O) $(CJSON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_manifest.c $(MANIFEST_O) $(CONFIG_O) $(TOML_O) $(SKILL_O) $(CJSON_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_asap_envelope: tests/test_asap_envelope.c $(ENVELOPE_O) $(CJSON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_asap_envelope.c $(ENVELOPE_O) $(CJSON_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_asap_ulid: tests/test_asap_ulid.c $(ULID_O) $(CRYPTO_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -pthread -o $(BINDIR)/$@ tests/test_asap_ulid.c $(ULID_O) $(CRYPTO_O) -pthread
	$(DSYM_SCRIPT)

test_asap_client: tests/test_asap_client.c $(CLIENT_O) $(ENVELOPE_O) $(ULID_O) $(CRYPTO_O) $(CJSON_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -pthread -o $(BINDIR)/$@ tests/test_asap_client.c $(CLIENT_O) $(ENVELOPE_O) $(ULID_O) $(CRYPTO_O) $(CJSON_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(LDLIBS) -pthread
	$(DSYM_SCRIPT)

test_asap_registry: tests/test_asap_registry.c $(ASAP_REGISTRY_TEST_O) $(CLIENT_O) $(ENVELOPE_O) $(ULID_O) $(CRYPTO_O) $(CJSON_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -DSHELLCLAW_REGISTRY_TEST -o $(BINDIR)/$@ tests/test_asap_registry.c $(ASAP_REGISTRY_TEST_O) $(CLIENT_O) $(ENVELOPE_O) $(ULID_O) $(CRYPTO_O) $(CJSON_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_asap_server: tests/test_asap_server.c $(SERVER_O) $(ENVELOPE_O) $(ULID_O) $(CRYPTO_O) $(CJSON_O) $(AGENT_O) $(ROUTER_O) $(STUB_O) $(ANTHROPIC_O) $(OPENAI_COMPAT_O) $(OPENAI_O) $(LOCAL_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(MEMORY_O) $(SKILL_O) $(SQLITE3_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -pthread -o $(BINDIR)/$@ tests/test_asap_server.c $(SERVER_O) $(ENVELOPE_O) $(ULID_O) $(CRYPTO_O) $(CJSON_O) $(AGENT_O) $(ROUTER_O) $(STUB_O) $(ANTHROPIC_O) $(OPENAI_COMPAT_O) $(OPENAI_O) $(LOCAL_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(MEMORY_O) $(SKILL_O) $(SQLITE3_O) $(LDLIBS) -pthread
	$(DSYM_SCRIPT)

test_asap_log: tests/test_asap_log.c $(ASAP_LOG_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -pthread -o $(BINDIR)/$@ tests/test_asap_log.c $(ASAP_LOG_O) -pthread
	$(DSYM_SCRIPT)

test_asap_invoke: tests/test_asap_invoke.c $(ASAP_INVOKE_TEST_O) $(ASAP_REGISTRY_TEST_O) $(CLIENT_O) $(ENVELOPE_O) $(ULID_O) $(CRYPTO_O) $(CJSON_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -DSHELLCLAW_ASAP_INVOKE_TEST -DSHELLCLAW_REGISTRY_TEST -o $(BINDIR)/$@ tests/test_asap_invoke.c $(ASAP_INVOKE_TEST_O) $(ASAP_REGISTRY_TEST_O) $(CLIENT_O) $(ENVELOPE_O) $(ULID_O) $(CRYPTO_O) $(CJSON_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_gateway_http: tests/test_gateway_http.c $(AUTH_O) $(CONFIG_O) $(TOML_O) $(CJSON_O)
	@if [ "$(GATEWAY)" != "1" ]; then \
		if [ "$(CI)" = "true" ]; then echo "test_gateway_http: GATEWAY=0 in CI — install libwebsockets-dev"; exit 1; fi; \
		echo "test_gateway_http: skipped (GATEWAY=0)"; exit 0; \
	fi; \
	mkdir -p $(BINDIR) && \
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -DSHELLCLAW_GATEWAY -o $(BINDIR)/$@ tests/test_gateway_http.c $(AUTH_O) $(CRYPTO_O) $(CONFIG_O) $(TOML_O) $(CJSON_O) $(LDLIBS) && \
	$(DSYM_SCRIPT)

test_static: tests/test_static.c src/gateway/ui_assets.h src/gateway/static.o
	@mkdir -p $(BINDIR)
	./scripts/embed_ui.sh
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_static.c src/gateway/static.o $(LDLIBS)
	$(DSYM_SCRIPT)

test_sandbox: tests/test_sandbox.c $(SANDBOX_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_sandbox.c $(SANDBOX_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_allowlist: tests/test_allowlist.c $(ALLOWLIST_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_allowlist.c $(ALLOWLIST_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_rate_limit: tests/test_rate_limit.c src/gateway/rate_limit.c src/gateway/rate_limit.h
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -pthread -o $(BINDIR)/$@ tests/test_rate_limit.c src/gateway/rate_limit.c -pthread
	$(DSYM_SCRIPT)

test_daemon_smoke: shellclaw
	@chmod +x tests/test_daemon_smoke.sh scripts/install.sh scripts/update.sh
	SHELLCLAW_TEST_BIN="$(BINDIR)/shellclaw" ./tests/test_daemon_smoke.sh

test_update_script: shellclaw
	@chmod +x tests/test_update_script.sh scripts/update.sh
	@./tests/test_update_script.sh

test_install_script:
	@chmod +x tests/test_install_script.sh scripts/install.sh
	@./tests/test_install_script.sh

test_web_dashboard:
	@if [ "$${CI:-}" = "true" ] && ! command -v node >/dev/null 2>&1; then \
		echo "test_web_dashboard: node required when CI=true" >&2; exit 1; \
	fi
	@if command -v node >/dev/null 2>&1; then node tests/test_web_dashboard.js; \
	else echo "test_web_dashboard: skipped (node not installed)"; fi

static:
	cppcheck --enable=warning,style,performance,portability --error-exitcode=1 \
		-I. -Isrc -Ivendor/tomlc99 -Ivendor/sqlite3 -Ivendor/cJSON \
		--suppress=missingIncludeSystem \
		--suppress=constVariablePointer \
		--suppress=knownConditionTrueFalse \
		--suppress=doubleFree:src/core/config.c \
		--suppress=constParameterPointer \
		--suppress=constParameterCallback \
		-q src/

test: test_config test_memory test_skill test_provider test_anthropic test_openai test_local_provider test_router test_heartbeat test_agent test_channel test_cli test_shell test_file test_telegram test_discord_helpers test_web_search test_cron test_context test_crypto test_hardware_stub test_ws test_manifest $(ASAP_UNIT_TESTS) test_sandbox test_allowlist test_rate_limit test_daemon_smoke test_update_script test_install_script test_web_dashboard
	$(BINDIR)/test_config
	$(BINDIR)/test_memory
	$(BINDIR)/test_skill
	$(BINDIR)/test_provider
	$(BINDIR)/test_anthropic
	$(BINDIR)/test_openai
	$(BINDIR)/test_local_provider
	$(BINDIR)/test_router
	$(BINDIR)/test_heartbeat
	$(BINDIR)/test_agent
	$(BINDIR)/test_channel
	$(BINDIR)/test_cli
	$(BINDIR)/test_shell
	$(BINDIR)/test_file
	$(BINDIR)/test_telegram
	$(BINDIR)/test_discord_helpers
	$(BINDIR)/test_web_search
	$(BINDIR)/test_cron
	$(BINDIR)/test_context
	$(BINDIR)/test_crypto
	$(BINDIR)/test_hardware_stub
	$(BINDIR)/test_ws
	$(BINDIR)/test_manifest
	@for t in $(ASAP_UNIT_TESTS); do $(BINDIR)/$$t || exit 1; done
	$(BINDIR)/test_sandbox
	$(BINDIR)/test_allowlist
	$(BINDIR)/test_rate_limit
	$(MAKE) test_install_script
	$(MAKE) test_web_dashboard
	$(MAKE) test_auth && $(BINDIR)/test_auth
	$(MAKE) test_static && $(BINDIR)/test_static
	@if [ "$(GATEWAY)" = "1" ]; then $(MAKE) test_gateway_http && $(BINDIR)/test_gateway_http; \
	elif [ "$(CI)" = "true" ]; then echo "GATEWAY=0 in CI — install libwebsockets-dev"; exit 1; fi

COVERAGE_DIR := build/coverage
COVERAGE_MIN := 80

coverage: clean
	$(MAKE) BUILD=coverage test_config test_memory test_skill test_provider test_anthropic test_openai test_local_provider test_router test_heartbeat test_agent test_channel test_cli test_shell test_file test_telegram test_discord_helpers test_web_search test_cron test_context test_crypto test_hardware_stub test_manifest $(ASAP_UNIT_TESTS) test_sandbox test_allowlist test_rate_limit test_auth test_static
	@if [ "$(GATEWAY)" = "1" ]; then $(MAKE) BUILD=coverage test_gateway_http; fi
	@chmod +x scripts/coverage.sh
	@BINDIR=$(BINDIR) COVERAGE_DIR=$(COVERAGE_DIR) COVERAGE_MIN=$(COVERAGE_MIN) GATEWAY=$(GATEWAY) ./scripts/coverage.sh

# Remove build artifacts left in repo root by old Makefiles (binaries and .dSYM)
clean-root-dsym:
	@for d in shellclaw test_agent test_anthropic test_channel test_cli test_config test_file test_memory test_local_provider test_openai test_provider test_router test_shell test_skill test_telegram test_web_search test_ws; do rm -rf $$d.dSYM; done
	@rm -f shellclaw test_agent test_anthropic test_channel test_cli test_config test_file test_memory test_local_provider test_openai test_provider test_router test_shell test_skill test_telegram test_web_search test_ws

clean: clean-root-dsym
	rm -f $(OBJS) $(PROVIDER_COMMON_O) $(STUB_O) $(ANTHROPIC_O) $(OPENAI_COMPAT_O) $(OPENAI_O) $(LOCAL_O) $(ROUTER_O) $(CJSON_O) $(ANTHROPIC_TEST_O) $(OPENAI_TEST_O) $(LOCAL_TEST_O) $(CONTEXT_TEST_OBJS) $(HEARTBEAT_TEST_O) $(CHANNEL_TG_TEST_O) $(CHANNEL_COMMON_O) $(CHANNEL_STUB_O) $(CHANNEL_CLI_O) $(CHANNEL_TG_O) $(CHANNEL_DISCORD_O) $(DISCORD_HELPERS_O) $(CHANNEL_HEARTBEAT_O) $(CHANNEL_WEBCHAT_O) $(AUTH_O) $(STATIC_O) $(HTTP_O) $(HTTP_LWS_O) $(ROUTES_O) $(WS_O) $(MANIFEST_O) $(ENVELOPE_O) $(ULID_O) $(CLIENT_O) $(ASAP_REGISTRY_O) $(SERVER_O) $(ASAP_LOG_O) $(RATE_LIMIT_O) $(SHELL_O) $(WEBSEARCH_O) $(FILE_O) $(REGISTRY_O) $(CONTEXT_O) $(CONTEXT_CACHE_O) $(CONTEXT_HTTP_O) $(CONTEXT_GEO_O) $(CRYPTO_O) $(HARDWARE_STUB_O) $(CRON_O) $(ASAP_INVOKE_O) $(SANDBOX_O) $(ALLOWLIST_O)
	rm -f src/gateway/ui_assets.h
	find . -name '*.gcno' -o -name '*.gcda' -o -name '*.gcov' | xargs rm -f 2>/dev/null || true
	rm -f $(WS_TEST_O) $(BINDIR)/asap_registry_test.o $(BINDIR)/asap_invoke_test.o $(CONTEXT_TEST_OBJS) $(HEARTBEAT_TEST_O) $(BINDIR)/shellclaw $(BINDIR)/test_config $(BINDIR)/test_memory $(BINDIR)/test_skill $(BINDIR)/test_provider $(BINDIR)/test_anthropic $(BINDIR)/test_openai $(BINDIR)/test_local_provider $(BINDIR)/test_router $(BINDIR)/test_heartbeat $(BINDIR)/test_crypto $(BINDIR)/test_hardware_stub $(BINDIR)/test_ws $(BINDIR)/test_agent $(BINDIR)/test_channel $(BINDIR)/test_cli $(BINDIR)/test_shell $(BINDIR)/test_file $(BINDIR)/test_telegram $(BINDIR)/test_discord_helpers $(BINDIR)/test_web_search $(BINDIR)/test_cron $(BINDIR)/test_context $(BINDIR)/test_manifest $(BINDIR)/test_asap_envelope $(BINDIR)/test_asap_ulid $(BINDIR)/test_asap_client $(BINDIR)/test_asap_registry $(BINDIR)/test_asap_server $(BINDIR)/test_asap_invoke $(BINDIR)/test_asap_log $(BINDIR)/test_auth $(BINDIR)/test_gateway_http $(BINDIR)/test_static $(BINDIR)/test_sandbox $(BINDIR)/test_allowlist $(BINDIR)/test_rate_limit
	rm -rf $(BINDIR)/*.dSYM $(DSYMDIR)
