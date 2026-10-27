#!/bin/bash
# HMB|TEC Persona Light V0.3.16 – Backup, Patch, Build, Flash
set -euo pipefail
VERSION='V0.3.16'
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ZIP="${1:-$SCRIPT_DIR/HMB_Persona_Light_MCP_V0.3.16.zip}"
PROJECT="${2:-$HOME/Documents/xiaozhi-persona-light}"
TARGET="$PROJECT/main/boards/sensecap-watcher"

fail() { echo "FEHLER: $*" >&2; exit 1; }
[[ -f "$ZIP" ]] || fail "Patch-ZIP nicht gefunden: $ZIP"
[[ -d "$TARGET" && -f "$PROJECT/sdkconfig" ]] || fail "ESP-IDF-Projekt nicht gefunden: $PROJECT"
command -v unzip >/dev/null || fail 'unzip fehlt'
command -v idf.py >/dev/null || fail 'idf.py fehlt – zuerst ESP-IDF-Umgebung aktivieren'

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
unzip -q "$ZIP" -d "$TMP"
SRC="$TMP/sensecap-watcher"
FILES=(persona_version.h sensecap_watcher.cc sscma_camera.cc)
for file in "${FILES[@]}"; do
  [[ -s "$SRC/$file" ]] || fail "Fehlende oder leere Datei im ZIP: $file"
done
# Sicherstellen, dass der Patch wirklich die erwartete Version trägt.
grep -Fq '#define HMB_PERSONA_VERSION "V0.3.16"' "$SRC/persona_version.h" || fail 'Version im ZIP stimmt nicht'
grep -Fq 'HMB_PERSONA_ANALYZE' "$SRC/sensecap_watcher.cc" || fail 'MCP-Trigger im Patch fehlt'

STAMP="$(date +%Y%m%d_%H%M%S)"
BACKUP="$PROJECT/persona_backups/$STAMP"
mkdir -p "$BACKUP"
for file in "${FILES[@]}"; do
  [[ -f "$TARGET/$file" ]] && cp -p "$TARGET/$file" "$BACKUP/$file"
done
printf '\nBackup: %s\n' "$BACKUP"
for file in "${FILES[@]}"; do
  cp "$SRC/$file" "$TARGET/$file"
  echo "Installiert: $file"
done
# Erzwingt die Neukompilierung beider geänderten Übersetzungseinheiten.
touch "$TARGET/sensecap_watcher.cc" "$TARGET/sscma_camera.cc"

echo 'Versionsprüfung:'
grep -n 'HMB_PERSONA_VERSION' "$TARGET/persona_version.h"
cd "$PROJECT"
echo 'Baue Firmware ...'
idf.py build
[[ -f build/xiaozhi.bin ]] || fail 'build/xiaozhi.bin nicht gefunden'
echo 'Flashe Firmware ...'
idf.py flash
echo "FERTIG: $VERSION – Backup: $BACKUP"
