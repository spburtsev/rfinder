#!/bin/bash

if [ ! -d "out" ]; then
  mkdir out
fi

DEBUG=0
if [ "$1" == "debug" ]; then
  DEBUG=1
fi

CXXFLAGS="-O2 -Wall -Wextra -Wpedantic -std=c++17"
if [ $DEBUG -eq 1 ]; then
  CXXFLAGS+=" -g -DDEBUG"
fi

OUTPUT_DIR="out"
OUT="$OUTPUT_DIR/rfinder-client"
FILES="client_main.cpp protocol.cpp"

echo "Building client..."
clang++ $CXXFLAGS $FILES -o $OUT
