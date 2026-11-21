#!/bin/bash
# Windows x64 için derleme (Linux/macOS'ta: pip install ziglang)
set -e
cd "$(dirname "$0")"
mkdir -p dist
python3 -m ziglang c++ -target x86_64-windows-gnu -O2 -std=c++17 -Wno-dll-attribute-on-redeclaration -shared -s \
  -o dist/ModelThumbs.dll src/provider.cpp \
  -lole32 -luuid -lgdi32 -ladvapi32 -lshell32
python3 -m ziglang c++ -target x86_64-windows-gnu -O2 -std=c++17 -s \
  -o dist/thumbtest.exe src/test_core.cpp
echo "BUILD-OK"
