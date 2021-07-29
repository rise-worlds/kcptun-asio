#!/bin/bash

cd gflags
git checkout . && git clean -xdf
cd ..

cd glog
git checkout . && git clean -xdf
cd ..

cd kcp
git checkout . && git clean -xdf
cd ..
