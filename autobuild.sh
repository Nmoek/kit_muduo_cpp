#!/bin/sh


mkdir -p build
mkdir -p bin

rm -r ./bin/*

cd ./build
cmake ..
echo "build...."
make -j4

echo "build finish!"
