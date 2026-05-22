#!/bin/sh
# Capture lcov per test and merge: gcda for shared .o files (config.o, etc.) does not
# accumulate reliably on Linux when dozens of binaries run in one process sequence.
# test_gateway_http still captures shellclaw subprocess hits in its own .info slice.
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
	TESTS="$TESTS test_daemon_smoke test_gateway_http"
fi

capture_info() {
	name="$1"
	info="$COVERAGE_DIR/${name}.info"
	if lcov --capture --directory src --output-file "$info" --rc "$LCOV_RC" 2>/dev/null; then
		return 0
	fi
	lcov --capture --directory . --output-file "$info" --rc "$LCOV_RC" 2>/dev/null || true
}

merge_info() {
	info="$1"
	all="$2"
	if [ ! -s "$info" ]; then
		echo "coverage: skip empty $(basename "$info")"
		return 0
	fi
	if [ ! -s "$all" ]; then
		cp "$info" "$all"
		return 0
	fi
	if lcov -a "$all" -a "$info" -o "${all}.tmp" --rc "$LCOV_RC" 2>/dev/null; then
		mv "${all}.tmp" "$all"
	else
		rm -f "${all}.tmp"
		echo "coverage: warning: failed to merge $(basename "$info") (skipping)"
	fi
}

ALL_INFO="$COVERAGE_DIR/all.info"
for t in $TESTS; do
	find . -name '*.gcda' -delete 2>/dev/null || true
	case "$t" in
	test_daemon_smoke)
		if [ ! -x "$BINDIR/shellclaw" ]; then
			echo "coverage: skip test_daemon_smoke (shellclaw not built)"
			continue
		fi
		if ! SHELLCLAW_TEST_BIN="$BINDIR/shellclaw" ./tests/test_daemon_smoke.sh; then
			echo "coverage: test_daemon_smoke failed"
			exit 1
		fi
		;;
	*)
		exe="$BINDIR/$t"
		if [ ! -f "$exe" ]; then
			echo "coverage: skip $t (not built)"
			continue
		fi
		if ! "$exe"; then
			echo "coverage: $t failed"
			exit 1
		fi
		;;
	esac
	capture_info "$t"
	merge_info "$COVERAGE_DIR/${t}.info" "$ALL_INFO"
done

if [ ! -s "$ALL_INFO" ]; then
	echo "Could not collect coverage data"
	exit 1
fi

CORE_INFO="$COVERAGE_DIR/core.info"
# Phase 4 integration-only (shellclaw + test_gateway_http / daemon smoke): not unit-isolated.
lcov --remove "$ALL_INFO" '/usr/*' 'vendor/*' 'tests/*' '*/channels/*' '*/tools/*' '*/providers/*' '*/core/main.c' \
	'*/gateway/http_lws.c' '*/gateway/routes.c' '*/core/bootstrap.c' '*/core/daemon.c' '*/core/dispatch.c' \
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
