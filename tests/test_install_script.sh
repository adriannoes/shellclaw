#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sandbox="$(mktemp -d)"
trap 'rm -rf "${sandbox}"' EXIT

export HOME="${sandbox}/home"
export XDG_CONFIG_HOME="${HOME}/.config"
mkdir -p "${HOME}"

bash "${ROOT}/scripts/install.sh"

unit_dir="${XDG_CONFIG_HOME}/systemd/user"
test -f "${unit_dir}/shellclaw.service"
test -f "${unit_dir}/llama-server.service"
grep -q 'shellclaw.service' "${unit_dir}/llama-server.service" || true

echo "test_install_script: OK"
