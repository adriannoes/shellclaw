#!/bin/sh
# Run all coverage-instrumented tests sequentially, accumulate .gcda, capture once.
# Per-test isolation loses hits when executables share object files (e.g. shellclaw
# subprocess from test_gateway_http writing gcda for http_lws.o while unit tests
# cover config.o in the same run).
set -e
BINDIR="${BINDIR:-build}"
COVERAGE_DIR="${COVERAGE_DIR:-build/coverage}"
COVERAGE_MIN="${COVERAGE_MIN:-80}"
LCOV_RC="lcov_branch_coverage=0"

mkdir -p "$COVERAGE_DIR"
rm -f "$COVERAGE_DIR"/*.info

TESTS="test_config test_memory test_skill test_provider test_anthropic test_openai test_local_provider test_router test_heartbeat test_agent test_channel test_cli test_shell test_file test_telegram test_discord_helpers test_web_search test_cron test_context test_crypto test_hardware_stub test_ws test_manifest test_asap_envelope test_asap_ulid test_asap_client test_asap_registry test_asap_server test_asap_invoke test_asap_log test_sandbox test_allowlist test_rate_limit test_auth test_static"
# ASAP tests must stay aligned with ASAP_UNIT_TESTS in the top-level Makefile.
if [ "${GATEWAY:-}" = "1" ]; then
	TESTS="$TESTS test_gateway_http"
fi

find . -name '*.gcda' -delete 2>/dev/null || true
for t in $TESTS; do
	exe="$BINDIR/$t"
	if [ ! -f "$exe" ]; then
		echo "coverage: skip $t (not built)"
		continue
	fi
	if ! "$exe"; then
		echo "coverage: $t failed"
		exit 1
	fi
done

ALL_INFO="$COVERAGE_DIR/all.info"
if ! lcov --capture --directory src --output-file "$ALL_INFO" --rc "$LCOV_RC" 2>/dev/null; then
	lcov --capture --directory . --output-file "$ALL_INFO" --rc "$LCOV_RC" 2>/dev/null || true
fi

if [ ! -s "$ALL_INFO" ]; then
	echo "Could not collect coverage data"
	exit 1
fi

CORE_INFO="$COVERAGE_DIR/core.info"
lcov --remove "$ALL_INFO" '/usr/*' 'vendor/*' 'tests/*' '*/channels/*' '*/tools/*' '*/providers/*' '*/core/main.c' \
	--output-file "$CORE_INFO" --rc "$LCOV_RC" --ignore-errors unused 2>/dev/null || true

pct=$(lcov --summary "$CORE_INFO" 2>/dev/null | grep 'lines' | grep -oE '[0-9]+\.?[0-9]*' | head -1 | cut -d. -f1)
if [ -n "$pct" ]; then
	if [ "$pct" -lt "$COVERAGE_MIN" ]; then
		echo "Coverage ${pct}% is below ${COVERAGE_MIN}%"
		lcov --list "$CORE_INFO" 2>/dev/null || true
		exit 1
	fi
	echo "Coverage: ${pct}% (>= ${COVERAGE_MIN}%)"
else
	echo "Could not parse coverage"
	exit 1
fi
genhtml "$CORE_INFO" -o "$COVERAGE_DIR/html" --quiet 2>/dev/null || true
