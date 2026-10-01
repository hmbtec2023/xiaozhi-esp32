# HMB | TEC – XiaoZhi Notizen

## Board wechseln

### Lichtblick Bread
python3 scripts/build.py hmbtec-lichtblick-bread --language de-DE

Auswahl:
2 = 128x64 OLED

### DualEye
python3 scripts/build.py hmbtec-dualeye --language de-DE

### BOARD_TYPE_HMBTEC_SPOTPEAR_128
python3 scripts/build.py hmbtec/hmbtec-spotpear-128 --language de-DE

ffmpeg -i test1.mp3 -ac 1 -ar 24000 -c:a libopus -b:a 24k -frame_duration 20 test1.ogg

mkdir -p ~/Desktop/XIAOZHI_AUDIO
for f in ~/Desktop/MP3/*.mp3; do
  ffmpeg -i "$f" -ac 1 -ar 24000 -c:a libopus -b:a 24k -vbr on -compression_level 10 -frame_duration 20 \
  ~/Desktop/XIAOZHI_AUDIO/"$(basename "${f%.*}").ogg"
done

----

## Aktuelles Board prüfen
grep -E "CONFIG_BOARD_TYPE_|CONFIG_OLED_" sdkconfig | grep "=y"

## ESP-IDF aktivieren
source /Users/hmb/.espressif/tools/activate_idf_v6.1.sh

## Merker
Boardwechsel immer über scripts/build.py.
Danach Build / Flash / Monitor wieder über ESP-IDF in VS Code.

