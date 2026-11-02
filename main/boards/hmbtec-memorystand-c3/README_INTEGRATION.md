# HMB|TEC MemoryStand C3 – Integrationshinweise

Diese ZIP enthaelt die drei Boarddateien fuer `main/boards/hmbtec-memorystand-c3/`.
Sie ist **kein vollstaendiger, bereits baubarer XiaoZhi-Patch**.

## Zusaetzlich erforderlich
1. Neue Boardvariante in der vorhandenen `main/Kconfig.projbuild` (oder entsprechender Boardauswahl) registrieren.
2. In `main/CMakeLists.txt` einen `elseif(CONFIG_BOARD_TYPE_HMBTEC_MEMORYSTAND_C3)`-Zweig anlegen und `boards/hmbtec-memorystand-c3/hmbtec_memorystand_c3_board.cc` in SOURCES sowie den Boardordner in INCLUDE_DIRS aufnehmen. An den vorhandenen Zweigen orientieren.
3. `NoAudioCodec::Read()`-Skalierung auf `>> 14` nur nach Abgleich mit dem aktuellen Quellstand anpassen; dieser ZIP liegt kein pauschaler Eingriff in die gemeinsam genutzte Codecdatei bei.
4. Duplex benutzt gemeinsame I2S-Takte. Daher sind fuer den ersten Test RX und TX auf **24 kHz** eingestellt; die zuvor im Arduino-Einzeltest verwendeten 16 kHz gelten nicht unveraendert fuer diesen XiaoZhi-Duplexmodus. Die korrekte Sprachaufnahme und Resampling-Kette muessen praktisch getestet werden.
5. Auf ESP32-C3 ist RAM/Flash fuer die ausgewaehlten XiaoZhi-Funktionen gesondert zu pruefen. Wake-Word-Modelle koennen unpassend sein.

## Pinbelegung
I2S BCLK=4 WS=5 MIC DIN=6 SPK DOUT=7; OLED SDA=20 SCL=21;
NeoPixel=8; IR RX=3 TX=10; LD2410 UART RX=1 TX=2.

## Status
Boarddateien vorbereitet; Build und Laufzeit noch **nicht verifiziert**.
