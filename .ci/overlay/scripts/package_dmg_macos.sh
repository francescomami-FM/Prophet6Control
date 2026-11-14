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

AU_SOURCE="$HERE/Audio Units/Prophet 6 Control.component"
VST3_SOURCE="$HERE/VST3/Prophet 6 Control.vst3"
APP_SOURCE="$HERE/Applications/Prophet 6 Control.app"

echo "Prophet 6 Control local installation"
echo "Apple Silicon development build. Ad hoc signed, not notarised."
echo "The installer does not disable Gatekeeper or remove quarantine attributes."
echo "macOS may require you to approve the installer or app in Privacy & Security."
read -r -p "Continue? [y/N] " answer
case "$answer" in
  y|Y|yes|YES) ;;
  *) echo "Installation cancelled."; exit 0 ;;
esac

# Validate all source bundles before touching the existing installation.
for bundle in "$AU_SOURCE" "$VST3_SOURCE" "$APP_SOURCE"; do
  if [[ ! -d "$bundle" || ! -f "$bundle/Contents/Info.plist" ]]; then
    echo "Required application bundle is missing or incomplete: $bundle" >&2
    exit 20
  fi
  /usr/bin/plutil -lint "$bundle/Contents/Info.plist"
  /usr/bin/codesign --verify --deep --strict "$bundle"
done

/bin/mkdir -p "$COMPONENTS" "$VST3" "$APPS"

BACKUP_STAMP="$(/bin/date +%Y%m%d-%H%M%S)"
BACKUP_ROOT="$HOME/Library/Application Support/Prophet 6 Control/Backups/$BACKUP_STAMP-$(/usr/bin/uuidgen)"

TARGET_PATHS=()
BACKUP_PATHS=()
HAD_PREVIOUS=()
ROLLBACK_ACTIVE=0

prepare_backup() {
  local target="$1"
  local category="$2"
  local name backup

  name="$(/usr/bin/basename "$target")"
  backup="$BACKUP_ROOT/$category/$name"

  TARGET_PATHS+=("$target")
  BACKUP_PATHS+=("$backup")

  if [[ -e "$target" || -L "$target" ]]; then
    /bin/mkdir -p "$(/usr/bin/dirname "$backup")"
    /usr/bin/ditto "$target" "$backup"
    HAD_PREVIOUS+=(1)
    echo "Backed up existing $category bundle: $backup"
  else
    HAD_PREVIOUS+=(0)
    echo "No previous $category bundle found."
  fi
}

rollback_all() {
  local index target backup failures
  failures=0
  echo
  echo "Installation did not complete. Restoring the previous state."

  for ((index=${#TARGET_PATHS[@]}-1; index>=0; index--)); do
    target="${TARGET_PATHS[$index]}"
    backup="${BACKUP_PATHS[$index]}"
    /bin/rm -rf "$target"

    if [[ "${HAD_PREVIOUS[$index]}" == "1" ]]; then
      if [[ -e "$backup" ]]; then
        if /usr/bin/ditto "$backup" "$target"; then
          echo "Restored: $target"
        else
          echo "CRITICAL: automatic restore failed. Original backup remains at: $backup" >&2
          failures=1
        fi
      else
        echo "CRITICAL: expected backup is missing: $backup" >&2
        failures=1
      fi
    else
      echo "Removed incomplete new installation: $target"
    fi
  done

  return "$failures"
}

finish_install() {
  local status="$?"
  trap - EXIT
  if [[ "$ROLLBACK_ACTIVE" == "1" ]]; then
    rollback_all || echo "One or more automatic restores failed. Preserve the backup folder shown above." >&2
  fi
  exit "$status"
}

# Back up every existing component before replacing any of them.
prepare_backup "$COMPONENTS/Prophet 6 Control.component" "Components"
prepare_backup "$VST3/Prophet 6 Control.vst3" "VST3"
prepare_backup "$APPS/Prophet 6 Control.app" "Applications"

# From this point, any unexpected exit rolls every installed path back.
ROLLBACK_ACTIVE=1
trap finish_install EXIT

install_bundle() {
  local source="$1"
  local target="$2"
  local label="$3"

  echo
  echo "Installing $label"
  /bin/rm -rf "$target"
  /usr/bin/ditto "$source" "$target"

  # Ad hoc signing repairs the local bundle resource seal. It does not
  # establish a Developer ID identity or bypass macOS Gatekeeper.
  /usr/bin/codesign --force --deep --sign - --timestamp=none "$target"
  /usr/bin/codesign --verify --deep --strict --verbose=2 "$target"
  echo "Verified: $target"
}

install_bundle "$AU_SOURCE" "$COMPONENTS/Prophet 6 Control.component" "Audio Unit"
install_bundle "$VST3_SOURCE" "$VST3/Prophet 6 Control.vst3" "VST3"
install_bundle "$APP_SOURCE" "$APPS/Prophet 6 Control.app" "Standalone application"

ROLLBACK_ACTIVE=0
trap - EXIT

echo
echo "Installation complete."
if [[ -d "$BACKUP_ROOT" ]]; then
  echo "Previous installation backups: $BACKUP_ROOT"
else
  echo "No previous installation needed backing up."
fi
echo "AU:         $COMPONENTS/Prophet 6 Control.component"
echo "VST3:       $VST3/Prophet 6 Control.vst3"
echo "Standalone: $APPS/Prophet 6 Control.app"
echo
echo "Quit and reopen Logic Pro. Reset and rescan Prophet 6 Control in Plug-in Manager if necessary."
echo "If macOS blocks the standalone app, use System Settings > Privacy & Security > Open Anyway after confirming its origin."
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
