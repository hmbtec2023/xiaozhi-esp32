# HMB | TEC – XiaoZhi Notizen

## ESP-IDF aktivieren
source /Users/hmb/.espressif/tools/activate_idf_v6.1.sh

## Aktuelles Board prüfen
grep -E "CONFIG_BOARD_TYPE_|CONFIG_OLED_" sdkconfig | grep "=y"

============================================================
 LICHTBLICK BREAD
============================================================

## 16 MB – Octal PSRAM (XH-S3E-AI_V1.0)

### Build
python3 scripts/build.py hmbtec-lichtblick-bread --name hmbtec-lichtblick-bread-128x64 --language de-DE

### Konfiguration prüfen
grep -E "CONFIG_ESPTOOLPY_FLASHSIZE_|CONFIG_SPIRAM_MODE_(QUAD|OCT)" sdkconfig

Erwartet:
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y

### Flashen
pkill -f idf_monitor.py
ls /dev/cu.usbmodem*

python -m esptool --chip esp32s3 -p /dev/cu.usbmodem101 -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0xd000 build/ota_data_initial.bin 0x20000 build/xiaozhi.bin 0x800000 build/generated_assets.bin


## 4 MB – Quad PSRAM (ESP32S3 SuperMini)

### Build
python3 scripts/build.py hmbtec-lichtblick-bread --name hmbtec-lichtblick-bread-128x64-4mb --language de-DE

### Konfiguration prüfen
grep -E "CONFIG_ESPTOOLPY_FLASHSIZE_|CONFIG_SPIRAM_MODE_(QUAD|OCT)" sdkconfig

Erwartet:
CONFIG_SPIRAM_MODE_QUAD=y
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y

### Flashen
pkill -f idf_monitor.py
ls /dev/cu.usbmodem*

python -m esptool --chip esp32s3 -p /dev/cu.usbmodem101 -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 4MB --flash-freq 80m 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0xd000 build/ota_data_initial.bin 0x10000 build/xiaozhi.bin 0x300000 build/generated_assets.bin


============================================================
 SPOTPEAR 1.28
============================================================

python3 scripts/build.py hmbtec/hmbtec-spotpear-128 --language de-DE


============================================================
 AUDIO
============================================================

## Einzeldatei
ffmpeg -i test1.mp3 -ac 1 -ar 24000 -c:a libopus -b:a 24k -frame_duration 20 test1.ogg

## Alle MP3-Dateien konvertieren
cd "$HOME/Desktop/OGG_Soundfiles" && mkdir -p audio && for f in *.mp3; do [ -e "$f" ] || continue; echo "Konvertiere: $f"; ffmpeg -y -i "$f" -ac 1 -ar 24000 -c:a libopus -b:a 24k -vbr on -compression_level 10 -frame_duration 20 "audio/${f%.mp3}.ogg"; done


============================================================
 MERKER
============================================================

Boardwechsel immer über scripts/build.py.

Lichtblick Bread:
16 MB = hmbtec-lichtblick-bread-128x64
        Octal PSRAM
        App    0x20000
        Assets 0x800000

4 MB  = hmbtec-lichtblick-bread-128x64-4mb
        Quad PSRAM
        App    0x10000
        Assets 0x300000

WICHTIG:
Nach einem Hardwarewechsel immer zuerst die passende Variante mit
scripts/build.py bauen.

Nicht einfach den vorhandenen build/ Ordner auf das andere Board flashen.

Danach Build / Flash / Monitor wieder über ESP-IDF in VS Code möglich.
