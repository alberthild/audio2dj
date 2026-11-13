#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
version=$(cat VERSION)
[[ -x dist/audio2dj-bridge && -d dist/Audio2DJ.driver ]] || { echo 'Run scripts/build.sh first.' >&2; exit 1; }
stage=$(mktemp -d "$PWD/build/package.XXXXXX")
trap 'rm -rf "$stage"' EXIT
app="$stage/Library/Application Support/Audio2DJExperimental"
mkdir -p "$app" "$stage/Library/Audio/Plug-Ins/HAL" "$stage/Library/LaunchAgents"
cp -R dist/Audio2DJ.driver "$stage/Library/Audio/Plug-Ins/HAL/"
cp dist/audio2dj-bridge "$app/"
cp scripts/launch-bridge.sh scripts/uninstall.sh "$app/"
cp LICENSE THIRD_PARTY_NOTICES.md "$app/"
mkdir -p "$app/licenses"
cp vendor/libASPL/LICENSE vendor/libASPL/LICENSE.apple2012 vendor/libASPL/LICENSE.apple2020 "$app/licenses/"
tar -xOf vendor/libusb-1.0.29.tar.bz2 libusb-1.0.29/COPYING > "$app/licenses/COPYING.libusb"
printf '%s\n' 'Corresponding source and relinking instructions:' 'https://github.com/alberthild/audio2dj' > "$app/SOURCE.txt"
cp packaging/org.local.audio2dj.bridge.plist "$stage/Library/LaunchAgents/"
chmod -R go-w "$stage"
/usr/bin/pkgbuild --root "$stage" --scripts packaging/scripts \
 --component-plist packaging/components.plist \
 --identifier org.local.audio2dj.experimental.pkg --version "$version" \
 --install-location / --ownership recommended "dist/Audio2DJ-Experimental-$version-intel.pkg"
echo 'Unsigned experimental package created; no Developer ID or notarization is claimed.'
