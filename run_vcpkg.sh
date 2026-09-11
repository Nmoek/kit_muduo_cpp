#!/usr/bin/env bash

# vcpkg 配置和加载

set -euo pipefail

if [[ -f .env ]]; then
    set -a
    . ./.env
    set +a
fi

echo "VCPKG_ROOT= $VCPKG_ROOT"
echo "VCPKG_DEFAULT_TRIPLET= $VCPKG_DEFAULT_TRIPLET"
echo "VCPKG_DISABLE_METRICS= $VCPKG_DISABLE_METRICS"
echo "VCPKG_DOWNLOADS= $VCPKG_DOWNLOADS"
echo "VCPKG_BUILD= $VCPKG_BUILD"

mkdir -p $VCPKG_BUILD

"$VCPKG_ROOT/vcpkg" install \
  --triplet x64-linux \
  --x-install-root="$VCPKG_BUILD" \
  --downloads-root="$VCPKG_DOWNLOADS"