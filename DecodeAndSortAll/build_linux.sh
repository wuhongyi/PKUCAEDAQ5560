#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
if ! command -v root-config >/dev/null 2>&1; then
    echo "CERN ROOT is not in PATH. First run: source /your/root/bin/thisroot.sh" >&2
    exit 1
fi
make -j"${JOBS:-4}" all
