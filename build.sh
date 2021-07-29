#!/bin/bash
N=1
if [ $# -gt 0  ]; then
    N=$1
fi
git submodule update --init --recursive

rm CMakeCache.txt 
cmake .
make clean 
make kcptun_client "-j$N"
make kcptun_server "-j$N"

