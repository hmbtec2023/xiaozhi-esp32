# HMB | TEC – XiaoZhi Notizen

## Board wechseln

### Lichtblick Bread
python3 scripts/build.py hmbtec-lichtblick-bread --name hmbtec-lichtblick-bread-128x64 --language de-DE


grep "CONFIG_BOARD_TYPE_HMBTEC_DUALEYE" sdkconfig
### BOARD_TYPE_HMBTEC_SPOTPEAR_128
python3 scripts/build.py hmbtec/hmbtec-spotpear-128 --language de-DE

ffmpeg -i test1.mp3 -ac 1 -ar 24000 -c:a libopus -b:a 24k -frame_duration 20 test1.ogg

========================================
 HMB | TEC - XiaoZhi Audio Converter
========================================
cd "$HOME/Desktop/OGG_Soundfiles" && mkdir -p audio && for f in *.mp3; do [ -e "$f" ] || continue; echo "Konvertiere: $f"; ffmpeg -y -i "$f" -ac 1 -ar 24000 -c:a libopus -b:a 24k -vbr on -compression_level 10 -frame_duration 20 "audio/${f%.mp3}.ogg"; done

----

## ESP-IDF aktivieren
source /Users/hmb/.espressif/tools/activate_idf_v6.1.sh

## Aktuelles Board prüfen
grep -E "CONFIG_BOARD_TYPE_|CONFIG_OLED_" sdkconfig | grep "=y"

## Build
python3 scripts/build.py hmbtec-lichtblick-bread --name hmbtec-lichtblick-bread-128x64 --language de-DE

## Flashen 16MB
pkill -f idf_monitor.py
 ls /dev/cu.usbmodem*
 python -m esptool --chip esp32s3 -p /dev/cu.usbmodem101 -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0xd000 build/ota_data_initial.bin 0x20000 build/xiaozhi.bin 0x800000 build/generated_assets.bin

## Flashen 4MB (SuperMini)
pkill -f idf_monitor.py
python -m esptool --chip esp32s3 -p /dev/cu.usbmodem101 -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 4MB --flash-freq 80m 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0xd000 build/ota_data_initial.bin 0x10000 build/xiaozhi.bin 0x300000 build/generated_assets.bin

## Merker
Boardwechsel immer über scripts/build.py.
Danach Build / Flash / Monitor wieder über ESP-IDF in VS Code.

