#!/bin/sh


mkdir -p build
mkdir -p bin

rm -r ./bin/*

cd build/
cmake -S .. -B .
echo "build...."
ninja

echo "build finish!"
