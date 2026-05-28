#!/bin/sh


mkdir -p build
mkdir -p bin

rm -r ./bin/*

cmake -S . -B build -G "Ninja"
echo "build...."
cmake --build build

echo "build finish!"
