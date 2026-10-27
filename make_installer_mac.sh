#!/bin/bash
# Builds Avel Ember and packs it into a double-click installer: dist/Avel Ember-<version>-macOS.pkg
# The installer puts VST3 and AU into /Library/Audio/Plug-Ins (for all users).
# Uses only tools that come with macOS / Xcode Command Line Tools.
set -e
cd "$(dirname "$0")"

VERSION=$(sed -n 's/^project(AvelEmber VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
[ -n "$VERSION" ] || { echo "Не нашёл версию в CMakeLists.txt"; exit 1; }

# 1. Build (skipped with --no-build, used by CI after its own build step)
if [ "$1" != "--no-build" ]; then
    bash ./build_mac.sh --build-only
fi

ART=build/AvelEmber_artefacts/Release
[ -d "$ART/VST3/Avel Ember.vst3" ] && [ -d "$ART/AU/Avel Ember.component" ] || { echo "Нет собранного плагина в $ART"; exit 1; }

echo "==> Собираю установщик Avel Ember $VERSION..."
WORK=build/pkg
rm -rf "$WORK"
mkdir -p "$WORK/root-vst3/Library/Audio/Plug-Ins/VST3" "$WORK/root-au/Library/Audio/Plug-Ins/Components" "$WORK/pkgs" dist

cp -R "$ART/VST3/Avel Ember.vst3" "$WORK/root-vst3/Library/Audio/Plug-Ins/VST3/"
cp -R "$ART/AU/Avel Ember.component" "$WORK/root-au/Library/Audio/Plug-Ins/Components/"
codesign --force --deep --sign - "$WORK/root-vst3/Library/Audio/Plug-Ins/VST3/Avel Ember.vst3"
codesign --force --deep --sign - "$WORK/root-au/Library/Audio/Plug-Ins/Components/Avel Ember.component"
xattr -cr "$WORK/root-vst3" "$WORK/root-au" 2>/dev/null || true

# Bundles must not be "relocatable", otherwise macOS Installer may write the update
# into some other copy of the plugin it finds on disk instead of /Library.
make_component_plist()
{
    local root="$1" plist="$2"
    pkgbuild --analyze --root "$root" "$plist" >/dev/null
    local i=0
    while /usr/libexec/PlistBuddy -c "Print :$i" "$plist" >/dev/null 2>&1; do
        # the key may be missing depending on the macOS version: delete + add works either way
        /usr/libexec/PlistBuddy -c "Delete :$i:BundleIsRelocatable" "$plist" >/dev/null 2>&1 || true
        /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$plist"
        /usr/libexec/PlistBuddy -c "Delete :$i:BundleOverwriteAction" "$plist" >/dev/null 2>&1 || true
        /usr/libexec/PlistBuddy -c "Add :$i:BundleOverwriteAction string upgrade" "$plist"
        i=$((i + 1))
    done
}

chmod +x packaging/macos/scripts/*

make_component_plist "$WORK/root-vst3" "$WORK/vst3.plist"
pkgbuild --root "$WORK/root-vst3" --component-plist "$WORK/vst3.plist" \
         --identifier com.avel.ember.vst3 --version "$VERSION" --install-location / \
         --scripts packaging/macos/scripts "$WORK/pkgs/AvelEmber-VST3.pkg"

make_component_plist "$WORK/root-au" "$WORK/au.plist"
pkgbuild --root "$WORK/root-au" --component-plist "$WORK/au.plist" \
         --identifier com.avel.ember.au --version "$VERSION" --install-location / \
         --scripts packaging/macos/scripts "$WORK/pkgs/AvelEmber-AU.pkg"

sed "s/@VERSION@/$VERSION/g" packaging/macos/distribution.xml > "$WORK/distribution.xml"

OUT="dist/AvelEmber-$VERSION-macOS.pkg"
rm -f "$OUT"
productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK/pkgs" \
             --resources packaging/macos/resources "$OUT"

echo
echo "Готово: $PWD/$OUT"
echo "Этот файл можно отправлять. Получателю: двойной клик -> Продолжить -> Установить."
