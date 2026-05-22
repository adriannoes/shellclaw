#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BIN_SRC="${ROOT}/build/shellclaw"
if [[ ! -x "${BIN_SRC}" ]]; then
	echo "test_update_script: build shellclaw first" >&2
	exit 1
fi

sandbox="$(mktemp -d)"
trap 'chmod -R u+w "${sandbox}" >/dev/null 2>&1 || true; rm -rf "${sandbox}"' EXIT

install_bin="${sandbox}/shellclaw"
staging="${sandbox}/incoming"
cp -f "${BIN_SRC}" "${staging}"

if command -v sha256sum >/dev/null 2>&1; then
	DIGEST="$(sha256sum "${staging}" | awk '{print $1}')"
else
	DIGEST="$(shasum -a 256 "${staging}" | awk '{print $1}')"
fi

stub="${sandbox}/stub-systemctl"
state_file="${sandbox}/svc.state"
stub_log="${sandbox}/calls.log"

cat >"${stub}" <<'STUB'
#!/usr/bin/env bash
set -euo pipefail

calls="${STUBCALLS:?}"
state="${STUBSTATE:?}"

{
	echo "$@"
} >>"${calls}"

case "${1:-}" in
	stop) rm -f "${state}"; exit 0 ;;
	start) mkdir -p "$(dirname "${state}")"; echo yes >"${state}"; exit 0 ;;
	restart) exit 0 ;;
	is-active) test -s "${state}"; exit $? ;;
	*) exit 0 ;;
esac

STUB

chmod +x "${stub}"

export STUBCALLS="${stub_log}"
export STUBSTATE="${state_file}"

run_update() {
	SHELLCLAW_INSTALL_BIN="${install_bin}" \
		SHELLCLAW_EXPECTED_SHA256="${1}" \
		UPDATE_SCRIPT_LOCAL_FILE="${staging}" \
		SYSTEMCTL="${stub}" \
		bash "${ROOT}/scripts/update.sh"
}

: >"${stub_log}"
run_update "${DIGEST}"

test -x "${install_bin}"
test -f "${install_bin}.bak"
grep -q '^stop ' "${stub_log}"
grep -q '^start ' "${stub_log}"

if run_update "0000000000000000000000000000000000000000000000000000000000000000"; then
	echo "test_update_script: expected invalid SHA256 to fail" >&2
	exit 1
fi

echo yes >"${state_file}"
echo "test_update_script: OK"
