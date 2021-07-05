#!/bin/bash
sudo rm -R build/*
cmake -Bbuild
cd build
make -j4 -o Linux64
cp ../kernels/* .
