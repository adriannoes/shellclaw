# ShellClaw Phase 1 — debug/release profiles, C11, libcurl for providers
CC     ?= gcc
BUILD  ?= debug
BINDIR ?= build
DSYMDIR ?= tests-dSYM
INC    := -I. -I src -I vendor/tomlc99 -I vendor/sqlite3 -I vendor/cJSON
LDLIBS := -lcurl -lm
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
# Channels (Task 6)
CHANNEL_COMMON_O := src/channels/channel_common.o
CHANNEL_STUB_O   := src/channels/stub.o
CHANNEL_CLI_O    := src/channels/cli.o
CHANNEL_TG_O     := src/channels/telegram.o
# Tools (Task 7)
SHELL_O    := src/tools/shell.o
WEBSEARCH_O := src/tools/web_search.o
FILE_O     := src/tools/file.o
REGISTRY_O := src/tools/registry.o
# Provider objects built with SHELLCLAW_TEST for negative/parse tests (CR-21)
ANTHROPIC_TEST_O := $(BINDIR)/anthropic_test.o
OPENAI_TEST_O    := $(BINDIR)/openai_test.o
CORE_OBJS := $(CONFIG_O) $(MAIN_O) $(MEMORY_O) $(SKILL_O) $(AGENT_O)
VENDOR_OBJS := $(TOML_O) $(SQLITE3_O) $(CJSON_O)
OBJS := $(CORE_OBJS) $(VENDOR_OBJS)
SQLITE_CFLAGS := -DSQLITE_ENABLE_FTS5

.PHONY: all debug release clean clean-root-dsym test shellclaw static coverage

all: debug

debug:
	$(MAKE) BUILD=debug shellclaw

release:
	$(MAKE) BUILD=release shellclaw

shellclaw: $(OBJS) $(PROVIDER_COMMON_O) $(STUB_O) $(ROUTER_O) $(ANTHROPIC_O) $(OPENAI_O) $(CHANNEL_COMMON_O) $(CHANNEL_CLI_O) $(CHANNEL_TG_O) $(SHELL_O) $(WEBSEARCH_O) $(FILE_O) $(REGISTRY_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(BINDIR)/$@ $(OBJS) $(PROVIDER_COMMON_O) $(STUB_O) $(ROUTER_O) $(ANTHROPIC_O) $(OPENAI_O) $(CHANNEL_COMMON_O) $(CHANNEL_CLI_O) $(CHANNEL_TG_O) $(SHELL_O) $(WEBSEARCH_O) $(FILE_O) $(REGISTRY_O) $(LDLIBS)
	$(DSYM_SCRIPT)
	@if [ "$(BUILD)" = "release" ]; then strip -s $(BINDIR)/$@ 2>/dev/null || true; fi

$(CONFIG_O): src/core/config.c src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<

$(MAIN_O): src/core/main.c src/core/config.h src/core/agent.h src/core/memory.h src/channels/channel.h src/providers/provider.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/core/main.c

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

$(PROVIDER_COMMON_O): src/providers/provider_common.c src/providers/provider.h
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

$(OPENAI_O): src/providers/openai.c src/providers/provider.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/providers/openai.c

$(OPENAI_TEST_O): src/providers/openai.c src/providers/provider.h src/core/config.h
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(INC) -DSHELLCLAW_TEST -c -o $@ src/providers/openai.c

$(ROUTER_O): src/providers/router.c src/providers/provider.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/providers/router.c

$(CHANNEL_COMMON_O): src/channels/channel_common.c src/channels/channel.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/channels/channel_common.c

$(CHANNEL_STUB_O): src/channels/stub.c src/channels/channel.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/channels/stub.c

$(CHANNEL_CLI_O): src/channels/cli.c src/channels/channel.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/channels/cli.c

$(CHANNEL_TG_O): src/channels/telegram.c src/channels/channel.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/channels/telegram.c

$(SHELL_O): src/tools/shell.c src/tools/tool.h src/tools/shell.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/tools/shell.c

$(WEBSEARCH_O): src/tools/web_search.c src/tools/tool.h src/tools/web_search.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/tools/web_search.c

$(FILE_O): src/tools/file.c src/tools/tool.h src/tools/file.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/tools/file.c

$(REGISTRY_O): src/tools/registry.c src/tools/tool.h src/tools/shell.h src/tools/web_search.h src/tools/file.h src/core/config.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/tools/registry.c

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

test_provider: tests/test_provider.c $(STUB_O) $(PROVIDER_COMMON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_provider.c $(STUB_O) $(PROVIDER_COMMON_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_anthropic: tests/test_anthropic.c $(ANTHROPIC_TEST_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(CJSON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -DSHELLCLAW_TEST -o $(BINDIR)/$@ tests/test_anthropic.c $(ANTHROPIC_TEST_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(CJSON_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_openai: tests/test_openai.c $(OPENAI_TEST_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(CJSON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -DSHELLCLAW_TEST -o $(BINDIR)/$@ tests/test_openai.c $(OPENAI_TEST_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(CJSON_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_router: tests/test_router.c $(ROUTER_O) $(STUB_O) $(ANTHROPIC_O) $(OPENAI_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(CJSON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_router.c $(ROUTER_O) $(STUB_O) $(ANTHROPIC_O) $(OPENAI_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(CJSON_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_agent: tests/test_agent.c $(AGENT_O) $(STUB_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(MEMORY_O) $(SKILL_O) $(SQLITE3_O) $(CJSON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_agent.c $(AGENT_O) $(STUB_O) $(PROVIDER_COMMON_O) $(CONFIG_O) $(TOML_O) $(MEMORY_O) $(SKILL_O) $(SQLITE3_O) $(CJSON_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_channel: tests/test_channel.c $(CHANNEL_COMMON_O) $(CHANNEL_STUB_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_channel.c $(CHANNEL_COMMON_O) $(CHANNEL_STUB_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_cli: tests/test_cli.c $(CHANNEL_COMMON_O) $(CHANNEL_CLI_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_cli.c $(CHANNEL_COMMON_O) $(CHANNEL_CLI_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_shell: tests/test_shell.c $(SHELL_O) $(CONFIG_O) $(TOML_O) $(CJSON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_shell.c $(SHELL_O) $(CONFIG_O) $(TOML_O) $(CJSON_O) $(LDLIBS)
	$(DSYM_SCRIPT)

test_file: tests/test_file.c $(FILE_O) $(REGISTRY_O) $(CONFIG_O) $(TOML_O) $(CJSON_O)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $(BINDIR)/$@ tests/test_file.c $(FILE_O) $(CONFIG_O) $(TOML_O) $(CJSON_O) $(LDLIBS)
	$(DSYM_SCRIPT)

static:
	cppcheck --enable=warning,style,performance,portability --error-exitcode=1 \
		-I. -Isrc -Ivendor/tomlc99 -Ivendor/sqlite3 -Ivendor/cJSON \
		--suppress=missingIncludeSystem \
		--suppress=constVariablePointer \
		--suppress=knownConditionTrueFalse \
		-q src/

test: test_config test_memory test_skill test_provider test_anthropic test_openai test_router test_agent test_channel test_cli test_shell test_file
	$(BINDIR)/test_config
	$(BINDIR)/test_memory
	$(BINDIR)/test_skill
	$(BINDIR)/test_provider
	$(BINDIR)/test_anthropic
	$(BINDIR)/test_openai
	$(BINDIR)/test_router
	$(BINDIR)/test_agent
	$(BINDIR)/test_channel
	$(BINDIR)/test_cli
	$(BINDIR)/test_shell
	$(BINDIR)/test_file

COVERAGE_DIR := build/coverage
COVERAGE_MIN := 80

coverage: clean
	$(MAKE) BUILD=coverage test
	@mkdir -p $(COVERAGE_DIR)
	lcov --capture --directory src --output-file $(COVERAGE_DIR)/all.info --rc lcov_branch_coverage=0 2>/dev/null || \
		lcov --capture --directory . --output-file $(COVERAGE_DIR)/all.info --rc lcov_branch_coverage=0 2>/dev/null || \
		(echo "lcov not installed; run: sudo apt-get install lcov" && exit 1)
	lcov --remove $(COVERAGE_DIR)/all.info '/usr/*' 'vendor/*' 'tests/*' '*/channels/*' '*/tools/*' '*/providers/*' '*/core/main.c' --output-file $(COVERAGE_DIR)/core.info --rc lcov_branch_coverage=0 --ignore-errors unused
	@pct=$$(lcov --summary $(COVERAGE_DIR)/core.info 2>/dev/null | grep 'lines' | grep -oE '[0-9]+\.?[0-9]*' | head -1 | cut -d. -f1); \
	if [ -n "$$pct" ]; then \
		if [ "$$pct" -lt $(COVERAGE_MIN) ]; then \
			echo "Coverage $$pct% is below $(COVERAGE_MIN)%"; exit 1; \
		fi; \
		echo "Coverage: $$pct% (>= $(COVERAGE_MIN)%)"; \
	else \
		echo "Could not parse coverage"; exit 1; \
	fi
	genhtml $(COVERAGE_DIR)/core.info -o $(COVERAGE_DIR)/html --quiet 2>/dev/null || true

# Remove build artifacts left in repo root by old Makefiles (binaries and .dSYM)
clean-root-dsym:
	@for d in shellclaw test_agent test_anthropic test_channel test_cli test_config test_file test_memory test_openai test_provider test_router test_shell test_skill; do rm -rf $$d.dSYM; done
	@rm -f shellclaw test_agent test_anthropic test_channel test_cli test_config test_file test_memory test_openai test_provider test_router test_shell test_skill

clean: clean-root-dsym
	rm -f $(OBJS) $(PROVIDER_COMMON_O) $(STUB_O) $(ANTHROPIC_O) $(OPENAI_O) $(ROUTER_O) $(CJSON_O) $(ANTHROPIC_TEST_O) $(OPENAI_TEST_O) $(CHANNEL_COMMON_O) $(CHANNEL_STUB_O) $(CHANNEL_CLI_O) $(CHANNEL_TG_O) $(SHELL_O) $(WEBSEARCH_O) $(FILE_O) $(REGISTRY_O)
	find . -name '*.gcno' -o -name '*.gcda' -o -name '*.gcov' | xargs rm -f 2>/dev/null || true
	rm -f $(BINDIR)/shellclaw $(BINDIR)/test_config $(BINDIR)/test_memory $(BINDIR)/test_skill $(BINDIR)/test_provider $(BINDIR)/test_anthropic $(BINDIR)/test_openai $(BINDIR)/test_router $(BINDIR)/test_agent $(BINDIR)/test_channel $(BINDIR)/test_cli $(BINDIR)/test_shell $(BINDIR)/test_file
	rm -rf $(BINDIR)/*.dSYM $(DSYMDIR)
