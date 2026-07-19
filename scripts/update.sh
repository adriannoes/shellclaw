#!/usr/bin/env bash
#
# Upgrade the ShellClaw binary from a Release asset or local file (Phase 4 PRD §4.6).
# HTTPS is used for api.github.com and for browser_download_url release assets when downloading.
#
# SECURITY — supply chain: verify SHELLCLAW_EXPECTED_SHA256 against checksums published with the release,
# unless SKIP_SHA256_VERIFY=1 for controlled CI/local envelopes.
#

set -euo pipefail

: "${SYSTEMCTL:=systemctl --user}"

SERVICE_UNIT="${SHELLCLAW_SERVICE_UNIT:-shellclaw.service}"

INSTALL_BIN="${SHELLCLAW_INSTALL_BIN:-"${HOME%/}/.local/bin/shellclaw"}"

GITHUB_REPO="${SHELLCLAW_GITHUB_REPO:-asap-protocol/shellclaw}"

GITHUB_ASSET_NAME="${SHELLCLAW_RELEASE_ASSET_NAME:-}"

GITHUB_ASSET_SUBSTRING="${SHELLCLAW_RELEASE_ASSET_SUBSTRING:-shellclaw}"

tmpdir=""

cleanup_tmp() {
	if [[ -n "${tmpdir:-}" && -d "${tmpdir}" ]]; then
		rm -rf "${tmpdir}"
	fi
}

curl_tls() {
	curl --proto '=https' --tlsv1.2 --proto-redir '=https' -fsSL "$@"
}

verify_sha256_sum() {
	local file="$1"
	local expected="$2"
	if [[ -z "${expected}" ]]; then
		echo "${0}: missing digest" >&2
		exit 1
	fi
	if command -v sha256sum >/dev/null 2>&1; then
		echo "${expected}  ${file}" | sha256sum -c -
	else
		echo "${expected}  ${file}" | shasum -a 256 -c -
	fi
}

resolve_asset_url_python() {
	local json_path="$1"
	GITHUB_ASSET_NAME="${GITHUB_ASSET_NAME:-}" \
		GITHUB_ASSET_SUBSTRING="${GITHUB_ASSET_SUBSTRING:-shellclaw}" \
		PY_JSON_PATH="$json_path" python3 <<'PY'
import json
import os

exact = os.environ.get("GITHUB_ASSET_NAME", "").strip()
sub = os.environ.get("GITHUB_ASSET_SUBSTRING", "shellclaw")

with open(os.environ["PY_JSON_PATH"], "r", encoding="utf-8") as handle:
    meta = json.load(handle)

chosen = ""

for asset in meta.get("assets", []):
    name = asset.get("name") or ""
    browser = asset.get("browser_download_url") or ""
    if not browser:
        continue
    if exact and name == exact:
        chosen = browser
        break
    if (not exact) and sub and sub in name:
        chosen = browser
        break

print(chosen)
PY
}

download_asset_via_api_to() {
	local out="$1"
	local json_tmp
	json_tmp="$(mktemp)"
	curl_tls -H "Accept: application/vnd.github+json" \
		"https://api.github.com/repos/${GITHUB_REPO}/releases/latest" -o "${json_tmp}"

	url="$(resolve_asset_url_python "${json_tmp}")"
	rm -f "${json_tmp}"
	if [[ -z "${url}" ]]; then
		echo "${0}: could not locate release asset (set SHELLCLAW_RELEASE_URL or UPDATE_SCRIPT_LOCAL_FILE)" >&2
		exit 1
	fi
	curl_tls "${url}" -o "${out}"
}

extract_candidate_executable() {
	local archive="$1"
	local dest_exe="$2"
	if [[ "${archive}" == *.tar.gz ]] || [[ "${archive}" == *.tgz ]]; then
		local extract_dir="${tmpdir}/extract"
		local candidate="${extract_dir}/shellclaw"
		mkdir -p "${extract_dir}"
		if ! tar xzf "${archive}" -C "${extract_dir}" \
			--no-same-owner --no-same-permissions --no-absolute-names \
			shellclaw 2>/dev/null; then
			if ! tar xzf "${archive}" -C "${extract_dir}" \
				--no-same-owner --no-same-permissions --no-absolute-names \
				./shellclaw 2>/dev/null; then
				echo "${0}: tarball missing top-level shellclaw binary" >&2
				exit 1
			fi
		fi
		if [[ ! -f "${candidate}" ]]; then
			echo "${0}: expected ${candidate} after controlled extract" >&2
			exit 1
		fi
		install -m 0755 "${candidate}" "${dest_exe}"
		return 0
	fi
	install -m 0755 "${archive}" "${dest_exe}"
}

rollback_binary() {
	if [[ -f "${INSTALL_BIN}.bak" ]] && cp -f "${INSTALL_BIN}.bak" "${INSTALL_BIN}"; then
		(${SYSTEMCTL} start "${SERVICE_UNIT}") >/dev/null 2>&1 || true
	fi
}

main() {
	trap cleanup_tmp EXIT INT TERM HUP

	if [[ "$(id -u)" -eq 0 ]]; then
		echo "${0}: run as a normal user (systemctl --user / HOME install)" >&2
		exit 1
	fi

	tmpdir="$(mktemp -d)"
	local staged="${tmpdir}/staging.bin"
	expected="${SHELLCLAW_EXPECTED_SHA256:-}"

	if [[ -n "${UPDATE_SCRIPT_LOCAL_FILE:-}" ]]; then
		cp -f "${UPDATE_SCRIPT_LOCAL_FILE}" "${staged}"
	elif [[ -n "${SHELLCLAW_RELEASE_URL:-}" ]]; then
		curl_tls "${SHELLCLAW_RELEASE_URL}" -o "${staged}"
	else
		download_asset_via_api_to "${staged}"
	fi

	if [[ -z "${SKIP_SHA256_VERIFY:-}" ]]; then
		if [[ -z "${expected}" ]]; then
			echo "${0}: set SHELLCLAW_EXPECTED_SHA256 to the release SHA256 before upgrading" >&2
			exit 1
		fi
		verify_sha256_sum "${staged}" "${expected}"
	else
		echo "${0}: SKIP_SHA256_VERIFY=1 active (dangerous)" >&2
	fi

	mkdir -p "$(dirname "${INSTALL_BIN}")"

	if [[ -x "${INSTALL_BIN}" ]]; then
		cp -f "${INSTALL_BIN}" "${INSTALL_BIN}.bak"
	else
		install -m 0755 "${staged}" "${INSTALL_BIN}.bak"
	fi

	probe_exe="${tmpdir}/shellclaw.next"
	extract_candidate_executable "${staged}" "${probe_exe}"

	version_line="$("${probe_exe}" --version 2>/dev/null || true)"
	if [[ -z "${version_line}" ]]; then
		echo "${0}: candidate lacks working --version" >&2
		rm -rf "${tmpdir}"
		exit 1
	fi
	echo "${0}: staged version: ${version_line}"

	(${SYSTEMCTL} stop "${SERVICE_UNIT}") >/dev/null 2>&1 || true

	if ! mv -f "${probe_exe}" "${INSTALL_BIN}"; then
		rollback_binary
		exit 1
	fi

	service_started=""
	if (${SYSTEMCTL} start "${SERVICE_UNIT}") >/dev/null 2>&1; then
		service_started=1
	fi

	if [[ -n "${service_started}" ]]; then
		sleep 2
		if ! (${SYSTEMCTL} is-active "${SERVICE_UNIT}") >/dev/null 2>&1; then
			echo "${0}: service inactive after restart — rollback" >&2
			cp -f "${INSTALL_BIN}.bak" "${INSTALL_BIN}" || true
			(${SYSTEMCTL} restart "${SERVICE_UNIT}") >/dev/null 2>&1 || true
			exit 1
		fi
	fi

	trap - EXIT INT TERM HUP
	cleanup_tmp
	echo "${0}: install path ${INSTALL_BIN} updated"
}

main "$@"
