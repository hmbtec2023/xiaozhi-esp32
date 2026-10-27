#!/usr/bin/env bash
# HMB|TEC Persona Light – ZIP installieren, gezielt neu bauen, BIN prüfen, flashen.
# Aufruf im Projektordner: bash /pfad/zu/build_flash.sh [patch.zip]
set -Eeuo pipefail
PROJECT="$(pwd -P)"
TARGET="$PROJECT/main/boards/sensecap-watcher"
APP="$PROJECT/main/application.cc"
[[ -d "$TARGET" && -f "$APP" && -f "$PROJECT/CMakeLists.txt" ]] || { echo 'FEHLER: Bitte im XiaoZhi-Projektordner starten.' >&2; exit 1; }
command -v idf.py >/dev/null || { echo 'FEHLER: ESP-IDF-Umgebung nicht aktiviert (idf.py fehlt).' >&2; exit 1; }
command -v python3 >/dev/null || { echo 'FEHLER: python3 fehlt.' >&2; exit 1; }
# ZIP standardmäßig im Projektordner; expliziter Pfad ist ebenfalls möglich.
if [[ $# -gt 0 ]]; then ZIP="$1"; else
  ZIP="$(python3 - "$PROJECT" <<'PY'
import pathlib,re,sys
p=pathlib.Path(sys.argv[1]); matches=[]
for f in p.glob('*.zip'):
 m=re.search(r'V(\d+)\.(\d+)\.(\d+)(?=\.zip$)',f.name,re.I)
 if m and 'persona' in f.name.lower(): matches.append((tuple(map(int,m.groups())),f))
if not matches: sys.exit(2)
mx=max(v for v,_ in matches); candidates=[f for v,f in matches if v==mx]
if len(candidates)!=1:
 print('Mehrere ZIPs mit höchster Version gefunden; ZIP explizit angeben:',file=sys.stderr)
 for f in candidates: print(' ',f.name,file=sys.stderr)
 sys.exit(3)
print(candidates[0])
PY
)" || { echo 'FEHLER: Kein eindeutiges Persona-Light-ZIP gefunden.' >&2; exit 1; }
fi
[[ -f "$ZIP" ]] || { echo "FEHLER: ZIP fehlt: $ZIP" >&2; exit 1; }
ZIP="$(cd "$(dirname "$ZIP")" && pwd -P)/$(basename "$ZIP")"
VERSION="$(python3 - "$ZIP" <<'PY'
import pathlib,re,sys
m=re.search(r'V(\d+)\.(\d+)\.(\d+)(?=\.zip$)',pathlib.Path(sys.argv[1]).name,re.I)
if not m: sys.exit('FEHLER: ZIP-Name ohne Version Vx.y.z')
print('V'+'.'.join(m.groups()))
PY
)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
# ZIP-Pfade prüfen und extrahieren; niemals beliebige Dateien in das Projekt kopieren.
python3 - "$ZIP" "$TMP" "$VERSION" <<'PY'
import pathlib,sys,zipfile
src,dst,ver=sys.argv[1:]; root=pathlib.Path(dst)
with zipfile.ZipFile(src) as z:
 names=set(z.namelist())
 required=['sensecap-watcher/'+f for f in ('persona_version.h','sensecap_watcher.cc','sscma_camera.cc')]
 for name in required:
  if name not in names: sys.exit('FEHLER: ZIP-Datei fehlt: '+name)
  (root/name).parent.mkdir(parents=True,exist_ok=True)
  (root/name).write_bytes(z.read(name))
 if 'patch_application.py' in names:
  (root/'patch_application.py').write_bytes(z.read('patch_application.py'))
text=(root/'sensecap-watcher/persona_version.h').read_text()
if f'#define HMB_PERSONA_VERSION "{ver}"' not in text:
 sys.exit(f'FEHLER: Versionskonflikt: ZIP={ver}, persona_version.h abweichend')
print('ZIP geprüft:',ver)
PY
# Optionalen application.cc-Patch zunächst auf einer KOPIE testen.
PATCH_APP=0
if [[ -f "$TMP/patch_application.py" ]]; then
 cp "$APP" "$TMP/application.cc"
 python3 "$TMP/patch_application.py" "$TMP/application.cc"
 PATCH_APP=1
fi
STAMP="$(date +%Y%m%d_%H%M%S)"
BACKUP="$PROJECT/persona_backups/${VERSION}_${STAMP}"
mkdir -p "$BACKUP"
FILES=(persona_version.h sensecap_watcher.cc sscma_camera.cc)
for f in "${FILES[@]}"; do [[ ! -f "$TARGET/$f" ]] || cp -p "$TARGET/$f" "$BACKUP/$f"; done
if (( PATCH_APP )); then cp -p "$APP" "$BACKUP/application.cc"; cp "$TMP/application.cc" "$APP"; fi
for f in "${FILES[@]}"; do cp "$TMP/sensecap-watcher/$f" "$TARGET/$f"; done
# Auch Header-Abhängigkeiten und Board-Module neu kompilieren.
find "$TARGET" -maxdepth 1 -type f \( -name '*.cc' -o -name '*.cpp' -o -name '*.c' \) -exec touch {} +
touch "$APP" "$TARGET/persona_version.h"
LOG="$BACKUP/build.log"
printf 'VERSION=%s\nZIP=%s\nBACKUP=%s\n' "$VERSION" "$ZIP" "$BACKUP" | tee "$BACKUP/install.txt"
shasum -a 256 "$ZIP" | tee -a "$BACKUP/install.txt"
echo 'Baue Firmware ...'
if ! idf.py build 2>&1 | tee "$LOG"; then echo "FEHLER: Build fehlgeschlagen. Log: $LOG" >&2; exit 1; fi
BIN="$PROJECT/build/xiaozhi.bin"
[[ -s "$BIN" ]] || { echo "FEHLER: Firmware fehlt: $BIN" >&2; exit 1; }
if ! LC_ALL=C grep -aFq -- "$VERSION" "$BIN"; then
 echo "FEHLER: Version $VERSION nicht in $BIN gefunden – kein Flash!" >&2
 echo 'Hinweis: Versionsstring muss im kompilierten Programm referenziert werden.' >&2
 exit 1
fi
printf 'BIN_SHA256=' >> "$BACKUP/install.txt"
shasum -a 256 "$BIN" | awk '{print $1}' | tee -a "$BACKUP/install.txt"
echo "Firmware $VERSION geprüft. Starte Flash ..."
if ! idf.py flash 2>&1 | tee "$BACKUP/flash.log"; then echo "FEHLER: Flash fehlgeschlagen. Log: $BACKUP/flash.log" >&2; exit 1; fi
echo "ERFOLG: $VERSION geflasht. Backup/Logs: $BACKUP"
