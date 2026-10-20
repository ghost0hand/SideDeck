#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
pwsh.exe -NoProfile -File ./build.ps1 -Configuration Debug