#!/bin/sh


set -eu

set -a
. ./.env
set +a

mkdir -p build
mkdir -p bin

rm -r ./bin/*

codegraph sync

cmake --preset linux-release
echo "build...."
cmake --build --preset linux-release

echo "build finish!"
