#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
VERSION="${VERSION:-0.2.4}"
ARTIFACTS="$BUILD_DIR/Prophet6Control_artefacts/Release"
DIST="$ROOT/dist"
STAGE="$DIST/Prophet 6 Control Installer"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "A genuine macOS DMG must be created on macOS with hdiutil." >&2
  exit 10
fi
command -v hdiutil >/dev/null 2>&1 || { echo "hdiutil was not found." >&2; exit 11; }
command -v codesign >/dev/null 2>&1 || { echo "codesign was not found." >&2; exit 12; }
[[ -d "$ARTIFACTS" ]] || { echo "Build artefacts not found: $ARTIFACTS" >&2; exit 13; }

find_bundle() { find "$ARTIFACTS" -type d -name "$1" -print -quit; }

AU_BUNDLE="$(find_bundle '*.component')"
VST3_BUNDLE="$(find_bundle '*.vst3')"
APP_BUNDLE="$(find_bundle '*.app')"

[[ -n "$AU_BUNDLE" ]] || { echo "Audio Unit component was not produced." >&2; exit 14; }
[[ -n "$VST3_BUNDLE" ]] || { echo "VST3 component was not produced." >&2; exit 15; }
[[ -n "$APP_BUNDLE" ]] || { echo "Standalone application was not produced." >&2; exit 16; }

rm -rf "$STAGE"
mkdir -p "$STAGE/Audio Units" "$STAGE/VST3" "$STAGE/Applications"

sign_and_verify() {
  local bundle="$1"
  echo "Applying a local ad hoc signature: $bundle"
  /usr/bin/codesign --force --deep --sign - --timestamp=none "$bundle"
  /usr/bin/codesign --verify --deep --strict --verbose=2 "$bundle"
}

ditto "$AU_BUNDLE" "$STAGE/Audio Units/$(basename "$AU_BUNDLE")"
ditto "$VST3_BUNDLE" "$STAGE/VST3/$(basename "$VST3_BUNDLE")"
ditto "$APP_BUNDLE" "$STAGE/Applications/$(basename "$APP_BUNDLE")"

sign_and_verify "$STAGE/Audio Units/$(basename "$AU_BUNDLE")"
sign_and_verify "$STAGE/VST3/$(basename "$VST3_BUNDLE")"
sign_and_verify "$STAGE/Applications/$(basename "$APP_BUNDLE")"

cat > "$STAGE/Install.command" <<'INSTALLER'
#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
COMPONENTS="$HOME/Library/Audio/Plug-Ins/Components"
VST3="$HOME/Library/Audio/Plug-Ins/VST3"
APPS="$HOME/Applications"

echo "Prophet 6 Control local installation"
echo "This build is ad hoc signed and is not notarised."
echo "The installer will sign the installed bundles locally and remove the"
echo "download quarantine flag from these three Prophet 6 Control bundles only."
echo "It will not change Gatekeeper settings or other applications."
read -r -p "Continue? [y/N] " answer
case "$answer" in
  y|Y|yes|YES) ;;
  *) echo "Installation cancelled."; exit 0 ;;
esac

mkdir -p "$COMPONENTS" "$VST3" "$APPS"

BACKUP_STAMP="$(date +%Y%m%d-%H%M%S)"
BACKUP_ROOT="$HOME/Library/Application Support/Prophet 6 Control/Backups/$BACKUP_STAMP-$(/usr/bin/uuidgen)"

install_bundle() {
  local source="$1"
  local destination="$2"
  local category="$3"
  local name target backup had_previous
  name="$(basename "$source")"
  target="$destination/$name"
  backup="$BACKUP_ROOT/$category/$name"
  had_previous=0

  echo
  echo "Installing $name"

  # Preserve any existing installation before replacing it. If copying,
  # signing or verification of the new bundle fails, restore the backup.
  if [[ -e "$target" || -L "$target" ]]; then
    /bin/mkdir -p "$(dirname "$backup")"
    /usr/bin/ditto "$target" "$backup"
    had_previous=1
    echo "Previous version backed up to: $backup"
  fi

  /bin/rm -rf "$target"
  if ! /usr/bin/ditto "$source" "$target"; then
    echo "Could not copy the new bundle: $target" >&2
    /bin/rm -rf "$target"
    if [[ "$had_previous" -eq 1 ]]; then
      /usr/bin/ditto "$backup" "$target"
      echo "Previous version restored."
    fi
    return 1
  fi

  # These are locally built project bundles. Ad hoc signing repairs the
  # bundle resource seal; quarantine removal is limited to this installed
  # bundle because this personal development build has no notarisation ticket.
  if ! /usr/bin/codesign --force --deep --sign - --timestamp=none "$target"; then
    echo "Could not sign the new bundle: $target" >&2
    /bin/rm -rf "$target"
    if [[ "$had_previous" -eq 1 ]]; then
      /usr/bin/ditto "$backup" "$target"
      echo "Previous version restored."
    fi
    return 1
  fi

  /usr/bin/xattr -dr com.apple.quarantine "$target" 2>/dev/null || true

  if ! /usr/bin/codesign --verify --deep --strict --verbose=2 "$target"; then
    echo "New bundle failed signature verification: $target" >&2
    /bin/rm -rf "$target"
    if [[ "$had_previous" -eq 1 ]]; then
      /usr/bin/ditto "$backup" "$target"
      echo "Previous version restored."
    fi
    return 1
  fi

  echo "Verified: $target"
}

AU_COUNT=0
VST3_COUNT=0
APP_COUNT=0
while IFS= read -r -d '' bundle; do install_bundle "$bundle" "$COMPONENTS" "Components"; AU_COUNT=$((AU_COUNT + 1)); done < <(find "$HERE/Audio Units" -maxdepth 1 -type d -name '*.component' -print0)
while IFS= read -r -d '' bundle; do install_bundle "$bundle" "$VST3" "VST3"; VST3_COUNT=$((VST3_COUNT + 1)); done < <(find "$HERE/VST3" -maxdepth 1 -type d -name '*.vst3' -print0)
while IFS= read -r -d '' bundle; do install_bundle "$bundle" "$APPS" "Applications"; APP_COUNT=$((APP_COUNT + 1)); done < <(find "$HERE/Applications" -maxdepth 1 -type d -name '*.app' -print0)

[[ "$AU_COUNT" -gt 0 && "$VST3_COUNT" -gt 0 && "$APP_COUNT" -gt 0 ]] || {
  echo "Installation incomplete. AU=$AU_COUNT VST3=$VST3_COUNT Standalone=$APP_COUNT" >&2
  exit 20
}

echo
echo "Installation complete."
if [[ -d "$BACKUP_ROOT" ]]; then
  echo "Previous installation backups: $BACKUP_ROOT"
else
  echo "No previous Prophet 6 Control installation needed backing up."
fi
echo "AU:        $COMPONENTS/Prophet 6 Control.component"
echo "VST3:      $VST3/Prophet 6 Control.vst3"
echo "Standalone: $APPS/Prophet 6 Control.app"
echo
echo "Quit and reopen Logic Pro. In Plug-in Manager, reset and rescan Prophet 6 Control if needed."
echo "Open the standalone from your home Applications folder to test the editor."
read -r -p "Press Return to close. " _
INSTALLER
chmod +x "$STAGE/Install.command"

cat > "$STAGE/README.txt" <<'README'
PROPHET 6 CONTROL v0.2.4
Apple Silicon local development build

CONTENTS
- Audio Unit component
- VST3 component
- Standalone application
- Local installation script

INSTALLATION
1. Mount the DMG.
2. Control-click Install.command and choose Open if macOS blocks the first launch.
3. Confirm the local installation in Terminal.
4. Quit and reopen Logic Pro.
5. If required, use Plug-in Manager to reset and rescan Prophet 6 Control.
6. Test the standalone application from ~/Applications.

INSTALL LOCATIONS
AU: ~/Library/Audio/Plug-Ins/Components
VST3: ~/Library/Audio/Plug-Ins/VST3
Standalone: ~/Applications

SIGNING AND SECURITY
This build is ad hoc signed and is not notarised. It is intended for local
personal development use, not public distribution. The installer applies a
local ad hoc signature, verifies each installed bundle, and removes the
macOS quarantine attribute from only these installed Prophet 6 Control
bundles. It does not disable Gatekeeper or change security settings globally.
Existing installations are backed up before replacement. A failed copy or
signing check restores the previous bundle when a backup exists.
Only install this build if you trust the project source and build.

WORKFLOW
Insert the Audio Unit as a MIDI effect in Logic Pro and route MIDI to the
Prophet 6 over USB. The standalone is for editor and connection testing.
Preset/project recall is designed to ask before sending a patch to hardware.

LIMITATION
A physical Prophet 6 and a running Logic Pro session are required to complete
hardware MIDI and host automation validation. The CI build does not claim
those hardware tests have been performed.
README

DMG="$DIST/Prophet-6-Control-v$VERSION.dmg"
mkdir -p "$DIST"
rm -f "$DMG"
hdiutil create -volname "Prophet 6 Control" -srcfolder "$STAGE" -fs HFS+ -ov -format UDZO "$DMG"
hdiutil verify "$DMG"
echo "Created and verified: $DMG"
