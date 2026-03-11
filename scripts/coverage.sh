#!/bin/sh
# Run each test in isolation and merge lcov coverage to avoid gcda corruption
# when multiple executables share the same object files.
set -e
BINDIR="${BINDIR:-build}"
COVERAGE_DIR="${COVERAGE_DIR:-build/coverage}"
COVERAGE_MIN="${COVERAGE_MIN:-80}"
LCOV_RC="lcov_branch_coverage=0"

mkdir -p "$COVERAGE_DIR"
rm -f "$COVERAGE_DIR"/*.info

TESTS="test_config test_memory test_skill test_provider test_anthropic test_openai test_router test_agent test_channel test_cli test_shell test_file test_telegram test_web_search test_cron test_manifest test_auth test_static"
if [ "$(GATEWAY 2>/dev/null)" = "1" ]; then
	TESTS="$TESTS test_gateway_http"
fi

for t in $TESTS; do
	exe="$BINDIR/$t"
	if [ ! -f "$exe" ]; then
		echo "coverage: skip $t (not built)"
		continue
	fi
	find . -name '*.gcda' -delete 2>/dev/null || true
	if ! "$exe"; then
		echo "coverage: $t failed"
		exit 1
	fi
	info="$COVERAGE_DIR/${t}.info"
	lcov --capture --directory src --output-file "$info" --rc "$LCOV_RC" 2>/dev/null || \
		lcov --capture --directory . --output-file "$info" --rc "$LCOV_RC" 2>/dev/null || true
done

ALL_INFO="$COVERAGE_DIR/all.info"
FIRST=""
for f in "$COVERAGE_DIR"/*.info; do
	[ -f "$f" ] || continue
	[ "$f" = "$ALL_INFO" ] && continue
	if [ -z "$FIRST" ]; then
		cp "$f" "$ALL_INFO"
		FIRST=1
	else
		lcov -a "$ALL_INFO" -a "$f" -o "${ALL_INFO}.tmp" --rc "$LCOV_RC" 2>/dev/null || true
		mv "${ALL_INFO}.tmp" "$ALL_INFO"
	fi
done

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
