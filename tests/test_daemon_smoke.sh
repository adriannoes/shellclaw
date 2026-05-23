#!/usr/bin/env bash
# Smoke daemon mode: HOME in a temp dir, verify pid/log and flock on double start.

set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BIN="${SHELLCLAW_TEST_BIN:-${ROOT}/build/shellclaw}"
if [[ ! -x "${BIN}" ]]; then
	echo "test_daemon_smoke: build shellclaw first (${BIN})" >&2

	exit 1
fi

TMP_HOME="$(mktemp -d)"
trap 'chmod -R u+w "${TMP_HOME}" >/dev/null 2>&1 || true; rm -rf "${TMP_HOME}"' EXIT
export HOME="${TMP_HOME}"

mkdir -p "${HOME}/.shellclaw"
CFG="${HOME}/.shellclaw/config.toml"
cat <<'EOF_CFG' >"${CFG}"

[agent]
model = "daemon-smoke-model"

EOF_CFG

detect_out="$("${BIN}" --detect-board)"
if [[ -z "${detect_out}" || "${detect_out}" == *$'\n'* ]]; then
	echo "test_daemon_smoke: --detect-board expected single non-empty line, got: ${detect_out@Q}" >&2
	exit 1
fi

stub_out="$(SHELLCLAW_BOARD=stub "${BIN}" --detect-board)"
if [[ "${stub_out}" != "stub" ]]; then
	echo "test_daemon_smoke: SHELLCLAW_BOARD=stub expected 'stub', got: ${stub_out@Q}" >&2
	exit 1
fi

"${BIN}" --daemon --config "${CFG}"

sleep 1
test -f "${HOME}/.shellclaw/shellclaw.pid"
test -f "${HOME}/.shellclaw/shellclaw.log"
running_pid="$(cat "${HOME}/.shellclaw/shellclaw.pid")"

if ! kill -0 "${running_pid}" 2>/dev/null; then
	echo "test_daemon_smoke: first daemon PID not alive" >&2
	exit 1
fi

set +e
"${BIN}" --daemon --config "${CFG}" >/dev/null 2>&1
set -e

sleep 1
if ! grep -Fq "another instance holds" "${HOME}/.shellclaw/shellclaw.log"; then
	echo "test_daemon_smoke: expected concurrent daemon lock message in log" >&2
	kill "${running_pid}"
	exit 1
fi

kill "${running_pid}"

wait "${running_pid}" 2>/dev/null || true


echo "test_daemon_smoke: OK"
