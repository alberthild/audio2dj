#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
[[ $(uname -s) == Darwin && $(uname -m) == x86_64 ]] || { echo 'Build requires an Intel Mac.' >&2; exit 1; }
[[ $(/usr/sbin/sysctl -n hw.optional.arm64 2>/dev/null || true) != 1 ]] || { echo 'Apple Silicon is not validated.' >&2; exit 1; }
command -v clang >/dev/null
mkdir -p build dist
expected=5977fc950f8d1395ccea9bd48c06b3f808fd3c2c961b44b0c2e6e29fc3a70a85
actual=$(shasum -a 256 vendor/libusb-1.0.29.tar.bz2 | awk '{print $1}')
[[ "$actual" == "$expected" ]] || { echo 'libusb checksum mismatch' >&2; exit 1; }
if [[ ! -d build/libusb-1.0.29 ]]; then tar -xjf vendor/libusb-1.0.29.tar.bz2 -C build; fi
(cd build/libusb-1.0.29 && ./configure --disable-shared --enable-static && make -j2)
clang -Wall -Wextra -O2 src/bridge.c -Ibuild/libusb-1.0.29/libusb \
 build/libusb-1.0.29/libusb/.libs/libusb-1.0.a \
 -framework IOKit -framework CoreFoundation -framework Security -o dist/audio2dj-bridge
mkdir -p dist/Audio2DJ.driver/Contents/MacOS
cp packaging/Info.plist dist/Audio2DJ.driver/Contents/Info.plist
clang++ -std=c++17 -O2 -fPIC -bundle -Wno-unused-parameter -Wno-invalid-offsetof \
 -Ivendor/libASPL/include src/Driver.cpp vendor/libASPL/src/*.cpp \
 -framework CoreFoundation -framework CoreAudio -o dist/Audio2DJ.driver/Contents/MacOS/Audio2DJ
codesign --force -s - dist/Audio2DJ.driver
codesign --verify --strict dist/Audio2DJ.driver
