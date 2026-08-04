#!/usr/bin/env bash
set -euo pipefail

if [[ -f .env ]]; then
    set -a
    . ./.env
    set +a
fi

if [[ "${1:-}" == "-gdb" ]]; then
    exec gdb --args ./bin/kit_protocol_test_platform
else
    exec ./bin/kit_protocol_test_platform
fi

