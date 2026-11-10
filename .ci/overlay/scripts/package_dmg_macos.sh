#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
VERSION="${VERSION:-0.2.2}"
ARTIFACTS="$BUILD_DIR/Prophet6Control_artefacts/Release"
DIST="$ROOT/dist"
STAGE="$DIST/Prophet 6 Control Installer"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "A genuine macOS DMG must be created on macOS with hdiutil." >&2
  exit 10
fi
command -v hdiutil >/dev/null 2>&1 || { echo "hdiutil was not found." >&2; exit 11; }
[[ -d "$ARTIFACTS" ]] || { echo "Build artefacts not found: $ARTIFACTS" >&2; exit 12; }

rm -rf "$STAGE"
mkdir -p "$STAGE/Audio Units" "$STAGE/VST3" "$STAGE/Applications"

find_bundle() { find "$ARTIFACTS" -type d -name "$1" -print -quit; }
AU_BUNDLE="$(find_bundle '*.component')"
VST3_BUNDLE="$(find_bundle '*.vst3')"
APP_BUNDLE="$(find_bundle '*.app')"
[[ -n "$AU_BUNDLE" ]] || { echo "Audio Unit component bundle was not produced." >&2; exit 13; }

ditto "$AU_BUNDLE" "$STAGE/Audio Units/$(basename "$AU_BUNDLE")"
[[ -z "$VST3_BUNDLE" ]] || ditto "$VST3_BUNDLE" "$STAGE/VST3/$(basename "$VST3_BUNDLE")"
[[ -z "$APP_BUNDLE" ]] || ditto "$APP_BUNDLE" "$STAGE/Applications/$(basename "$APP_BUNDLE")"

cat > "$STAGE/Install.command" <<'INSTALLER'
#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
COMPONENTS="$HOME/Library/Audio/Plug-Ins/Components"
VST3="$HOME/Library/Audio/Plug-Ins/VST3"
APPS="$HOME/Applications"
mkdir -p "$COMPONENTS" "$VST3" "$APPS"
copy_bundle() {
  local source="$1" destination="$2" name
  name="$(basename "$source")"
  rm -rf "$destination/$name"
  ditto "$source" "$destination/$name"
  echo "Installed: $destination/$name"
}
while IFS= read -r -d '' bundle; do copy_bundle "$bundle" "$COMPONENTS"; done < <(find "$HERE/Audio Units" -maxdepth 1 -type d -name '*.component' -print0)
while IFS= read -r -d '' bundle; do copy_bundle "$bundle" "$VST3"; done < <(find "$HERE/VST3" -maxdepth 1 -type d -name '*.vst3' -print0)
while IFS= read -r -d '' bundle; do copy_bundle "$bundle" "$APPS"; done < <(find "$HERE/Applications" -maxdepth 1 -type d -name '*.app' -print0)
echo
echo "Installation files copied. Restart Logic Pro and validate the plug-in in Plug-in Manager if necessary."
read -r -p "Press Return to close. " _
INSTALLER
chmod +x "$STAGE/Install.command"

cat > "$STAGE/README.txt" <<'README'
PROPHET 6 CONTROL v0.2.2

Install
1. Open Install.command. If blocked, Control-click and choose Open.
2. Restart Logic Pro.
3. Validate Prophet 6 Control in Plug-in Manager if required.
4. Insert Prophet 6 Control as a MIDI FX plug-in and route MIDI to the Prophet 6 USB port.

Installed locations
AU: ~/Library/Audio/Plug-Ins/Components
VST3: ~/Library/Audio/Plug-Ins/VST3
Standalone: ~/Applications

Preset and project recall asks for confirmation before any patch is sent to the hardware.
This development build is not Developer ID signed or notarised.
The physical Prophet 6 MIDI workflow still requires studio validation.
README

DMG="$DIST/Prophet-6-Control-v$VERSION.dmg"
mkdir -p "$DIST"
rm -f "$DMG"
# HFS+ maximises compatibility with older macOS versions; the image is compressed UDZO.
hdiutil create -volname "Prophet 6 Control" -srcfolder "$STAGE" -fs HFS+ -ov -format UDZO "$DMG"
echo "Created: $DMG"
