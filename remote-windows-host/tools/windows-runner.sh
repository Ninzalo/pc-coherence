#!/bin/sh
# Mac-side wrapper for the Windows build/test runner (plan Sections 18.3, 18.4).
# The remote side is tools/windows_runner.ps1, installed at C:\rwh\rwh-runner.ps1
# on the runner PC (SHA256-verified at install time). Only the allow-listed
# actions are reachable; the commit SHA is validated locally and remotely.
#
# Usage:
#   windows-runner.sh status
#   windows-runner.sh prepare <40-hex-sha>
#   windows-runner.sh build
#   windows-runner.sh unit-tests
#   windows-runner.sh selftest
#   windows-runner.sh probe-tray <40-hex-sha>   (window-creation diagnostic)
#   windows-runner.sh clean
#
# Configuration via environment (no secrets in this file or in the repo):
#   RWH_RUNNER_HOST  (default Ninzalo@192.168.100.139 — DHCP; update if it moves)
#   RWH_RUNNER_KEY   (default ~/.ssh/id_ed25519_pc)

set -eu

ACTION="${1:-status}"
case "$ACTION" in
  status|prepare|build|unit-tests|selftest|probe-tray|clean) ;;
  *) echo "unknown action: $ACTION (allow-list: status prepare build unit-tests selftest probe-tray clean)" >&2; exit 2 ;;
esac

SSH_HOST="${RWH_RUNNER_HOST:-Ninzalo@192.168.100.139}"
SSH_KEY="${RWH_RUNNER_KEY:-$HOME/.ssh/id_ed25519_pc}"

PS_SCRIPT="& 'C:\\rwh\\rwh-runner.ps1' -Action '$ACTION'"
if [ "$#" -gt 1 ]; then
  SHA="$2"
  if ! echo "$SHA" | grep -Eq '^[0-9a-f]{40}$'; then
    echo "invalid commit sha: $SHA" >&2
    exit 2
  fi
  PS_SCRIPT="$PS_SCRIPT -Sha '$SHA'"
fi

# Encode the remote script as UTF-16LE base64 and pass it via
# powershell -EncodedCommand (no shell metacharacter exposure).
B64=$(python3 -c 'import base64,sys; print(base64.b64encode(sys.argv[1].encode("utf-16-le")).decode())' "$PS_SCRIPT")

exec ssh -i "$SSH_KEY" -o BatchMode=yes "$SSH_HOST" \
  "powershell -NoProfile -ExecutionPolicy Bypass -EncodedCommand $B64"
