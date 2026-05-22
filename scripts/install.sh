#!/usr/bin/env bash
# Install helper: copy systemd user units next to ~/.config/systemd/user and print enable steps.
#
# Purpose: `./scripts/install.sh` — aligns with Phase 4 PRD §4.4.30.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UNIT_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
INSTALL_BIN="${SHELLCLAW_INSTALL_BIN:-${HOME%/}/.local/bin/shellclaw}"
SHELLCLAW_HOME="${SHELLCLAW_HOME:-${HOME%/}/.shellclaw}"

mkdir -p "${UNIT_DIR}"
install -m 0644 "${ROOT}/systemd/shellclaw.service" "${UNIT_DIR}/shellclaw.service"
install -m 0644 "${ROOT}/systemd/llama-server.service" "${UNIT_DIR}/llama-server.service"

echo "Installed units into ${UNIT_DIR}"
echo ""
if [[ ! -x "${INSTALL_BIN}" ]]; then
	echo "Warning: ShellClaw binary not found or not executable at ${INSTALL_BIN}" >&2
	echo "Build or copy the binary before starting the service (see README)." >&2
	echo ""
fi
echo "Environment (override before enable if needed):"
echo "  SHELLCLAW_INSTALL_BIN=${INSTALL_BIN}"
echo "  SHELLCLAW_HOME=${SHELLCLAW_HOME}"
echo ""
echo "Next:"
echo '  Prefix `systemctl` with `sudo` only if you use system-wide systemd instead of `--user`.'
echo "  systemctl --user daemon-reload"
echo "  systemctl --user enable --now shellclaw.service"
echo "Optional (local inference): edit ${UNIT_DIR}/llama-server.service ExecStart, then:"
echo "  systemctl --user enable --now llama-server.service"
