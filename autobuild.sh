#!/bin/sh

set -eu

set -a
. ./.env
set +a

rm -r ./bin

mkdir -p build
mkdir -p bin

cmake --preset linux-release
echo "build...."
cmake --build --preset linux-release

echo "build finish!"
