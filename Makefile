# ShellClaw Phase 1 — debug/release profiles, C11, libcurl for providers
CC     ?= gcc
BUILD  ?= debug
BINDIR ?= build
DSYMDIR ?= tests-dSYM
INC    := -I. -I src -I vendor/tomlc99 -I vendor/sqlite3 -I vendor/cJSON
LDLIBS := -lcurl
# On macOS debug builds: generate .dSYM into tests-dSYM/ and remove any from BINDIR
DSYM_SCRIPT = @mkdir -p $(DSYMDIR) && ( [ "$$(uname)" != "Darwin" ] || [ "$(BUILD)" != "debug" ] || dsymutil $(BINDIR)/$@ -o $(DSYMDIR)/$@.dSYM 2>/dev/null ); rm -rf $(BINDIR)/$@.dSYM

ifeq ($(BUILD),release)
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -DNDEBUG
else
CFLAGS ?= -std=c11 -Wall -Wextra -g -O0 -DDEBUG
endif

# Core
CONFIG_O  := src/core/config.o
MAIN_O    := src/core/main.o
MEMORY_O  := src/core/memory.o
SKILL_O   := src/core/skill.o
AGENT_O   := src/core/agent.o
# Vendor
TOML_O   := vendor/tomlc99/toml.o
SQLITE3_O := vendor/sqlite3/sqlite3.o
# Providers (Task 4)
PROVIDER_COMMON_O := src/providers/provider_common.o
STUB_O            := src/providers/stub.o
CJSON_O     := vendor/cJSON/cJSON.o
ANTHROPIC_O := src/providers/anthropic.o
OPENAI_O   := src/providers/openai.o
ROUTER_O   := src/providers/router.o
CORE_OBJS := $(CONFIG_O) $(MAIN_O) $(MEMORY_O) $(SKILL_O) $(AGENT_O)
VENDOR_OBJS := $(TOML_O) $(SQLITE3_O) $(CJSON_O)
OBJS := $(CORE_OBJS) $(VENDOR_OBJS)
SQLITE_CFLAGS := -DSQLITE_ENABLE_FTS5

.PHONY: all debug release clean clean-root-dsym test shellclaw

all: debug

debug:
	$(MAKE) BUILD=debug shellclaw

release:
	$(MAKE) BUILD=release shellclaw

shellclaw: $(OBJS) $(PROVIDER_COMMON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $(BINDIR)/$@ $(OBJS) $(PROVIDER_COMMON_O) $(LDLIBS)
	$(DSYM_SCRIPT)

$(CONFIG_O): src/core/config.c src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<

$(MAIN_O): src/core/main.c src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/core/main.c

$(TOML_O): vendor/tomlc99/toml.c vendor/tomlc99/toml.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<

$(SQLITE3_O): vendor/sqlite3/sqlite3.c vendor/sqlite3/sqlite3.h
	$(CC) $(CFLAGS) $(SQLITE_CFLAGS) $(INC) -c -o $@ $<

$(MEMORY_O): src/core/memory.c src/core/memory.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<

$(SKILL_O): src/core/skill.c src/core/skill.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/core/skill.c

$(AGENT_O): src/core/agent.c src/core/agent.h src/core/config.h src/core/memory.h src/core/skill.h src/providers/provider.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/core/agent.c

$(PROVIDER_COMMON_O): src/providers/provider_common.c src/providers/provider.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/providers/provider_common.c

$(STUB_O): src/providers/stub.c src/providers/provider.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/providers/stub.c

$(CJSON_O): vendor/cJSON/cJSON.c vendor/cJSON/cJSON.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ vendor/cJSON/cJSON.c

$(ANTHROPIC_O): src/providers/anthropic.c src/providers/provider.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/providers/anthropic.c

$(OPENAI_O): src/providers/openai.c src/providers/provider.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/providers/openai.c

$(ROUTER_O): src/providers/router.c src/providers/provider.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/providers/router.c

test_config: tests/test_config.c $(CONFIG_O) $(TOML_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_config.c $(CONFIG_O) $(TOML_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_memory: tests/test_memory.c $(MEMORY_O) $(SQLITE3_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_memory.c $(MEMORY_O) $(SQLITE3_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_skill: tests/test_skill.c $(SKILL_O) $(CONFIG_O) $(TOML_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_skill.c $(SKILL_O) $(CONFIG_O) $(TOML_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_provider: tests/test_provider.c $(STUB_O) $(PROVIDER_COMMON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_provider.c $(STUB_O) $(PROVIDER_COMMON_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_anthropic: tests/test_anthropic.c $(ANTHROPIC_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(CJSON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_anthropic.c $(ANTHROPIC_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(CJSON_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_openai: tests/test_openai.c $(OPENAI_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(CJSON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_openai.c $(OPENAI_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(CJSON_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_router: tests/test_router.c $(ROUTER_O) $(ANTHROPIC_O) $(OPENAI_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(CJSON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_router.c $(ROUTER_O) $(ANTHROPIC_O) $(OPENAI_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(CJSON_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_agent: tests/test_agent.c $(AGENT_O) $(STUB_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(MEMORY_O) $(SKILL_O) $(SQLITE3_O) $(CJSON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_agent.c $(AGENT_O) $(STUB_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(MEMORY_O) $(SKILL_O) $(SQLITE3_O) $(CJSON_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test: test_config test_memory test_skill test_provider test_anthropic test_openai test_router test_agent
	$(BINDIR)/test_config
	$(BINDIR)/test_memory
	$(BINDIR)/test_skill
	$(BINDIR)/test_provider
	$(BINDIR)/test_anthropic
	$(BINDIR)/test_openai
	$(BINDIR)/test_router
	$(BINDIR)/test_agent

# Remove build artifacts left in repo root by old Makefiles (binaries and .dSYM)
clean-root-dsym:
	@for d in shellclaw test_agent test_anthropic test_config test_memory test_openai test_provider test_router test_skill; do rm -rf $$d.dSYM; done
	@rm -f shellclaw test_agent test_anthropic test_config test_memory test_openai test_provider test_router test_skill

clean: clean-root-dsym
	rm -f $(OBJS) $(PROVIDER_COMMON_O) $(STUB_O) $(ANTHROPIC_O) $(OPENAI_O) $(ROUTER_O) $(CJSON_O)
	rm -f $(BINDIR)/shellclaw $(BINDIR)/test_config $(BINDIR)/test_memory $(BINDIR)/test_skill $(BINDIR)/test_provider $(BINDIR)/test_anthropic $(BINDIR)/test_openai $(BINDIR)/test_router $(BINDIR)/test_agent
	rm -rf $(BINDIR)/*.dSYM $(DSYMDIR)
