#!/bin/bash
# Builds Avel Ember (VST3 + AU) and installs it into ~/Library/Audio/Plug-Ins.
# Needs only Xcode Command Line Tools. CMake is downloaded into ./.tools if missing.
set -e
cd "$(dirname "$0")"

if ! xcode-select -p >/dev/null 2>&1; then
    echo "Нужны Xcode Command Line Tools. Сейчас откроется установщик; после установки запустите скрипт снова."
    xcode-select --install || true
    exit 1
fi

if command -v cmake >/dev/null 2>&1; then
    CMAKE=cmake
else
    V=3.30.5
    D=".tools/cmake-$V-macos-universal"
    if [ ! -x "$D/CMake.app/Contents/bin/cmake" ]; then
        echo "==> Скачиваю CMake $V (только в папку проекта)..."
        mkdir -p .tools
        curl -fL --progress-bar "https://github.com/Kitware/CMake/releases/download/v$V/cmake-$V-macos-universal.tar.gz" -o .tools/cmake.tgz
        tar -xzf .tools/cmake.tgz -C .tools
        rm -f .tools/cmake.tgz
    fi
    CMAKE="$PWD/$D/CMake.app/Contents/bin/cmake"
fi

echo "==> Настраиваю проект (первый раз скачивается JUCE)..."
"$CMAKE" -S . -B build -DCMAKE_BUILD_TYPE=Release

echo "==> Собираю..."
"$CMAKE" --build build --config Release --target AvelEmber_VST3 AvelEmber_AU -j "$(sysctl -n hw.ncpu)"

# make_installer_mac.sh only needs the build
if [ "$1" == "--build-only" ]; then
    exit 0
fi

ART=build/AvelEmber_artefacts/Release
VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"

echo "==> Устанавливаю..."
mkdir -p "$VST3_DIR" "$AU_DIR"
rm -rf "$VST3_DIR/Avel Ember.vst3" "$AU_DIR/Avel Ember.component"
cp -R "$ART/VST3/Avel Ember.vst3" "$VST3_DIR/"
cp -R "$ART/AU/Avel Ember.component" "$AU_DIR/"
codesign --force --deep --sign - "$VST3_DIR/Avel Ember.vst3"
codesign --force --deep --sign - "$AU_DIR/Avel Ember.component"
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo
echo "Готово:"
echo "  $VST3_DIR/Avel Ember.vst3"
echo "  $AU_DIR/Avel Ember.component"
echo "В REAPER: Preferences -> Plug-ins -> VST -> Re-scan."
