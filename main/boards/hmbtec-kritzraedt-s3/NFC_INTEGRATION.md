# HMB|TEC Kritzraedt – RC522 Software-SPI V0.2

## Anschluss (vierpoliger GPIO-Connector)

| RC522 | ESP32-S3 |
|---|---|
| SCK | GPIO14 |
| MOSI | GPIO21 |
| MISO | GPIO2 |
| SDA / SS / CS | GPIO3 |
| NRSTPD / RST | 3,3 V (HIGH; kein GPIO-Reset) |
| VCC | 3,3 V |
| GND | GND |

**Achtung:** GPIO3 ist ESP32-S3-Strapping-Pin. RC522-Modul darf den Startpegel nicht unerwuenscht beeinflussen. Vor Einbau Bootverhalten pruefen. Kein 5-V-Signal an ESP32-S3 GPIO.

## Firmware

- Software-SPI Mode 0; unabhaengig vom ILI9341-SPI-Bus.
- Buzzer GPIO2, HMB-Taster GPIO3, NeoPixel GPIO14 entfallen. Die betreffenden Initialisierungen waren bereits auskommentiert; Taster-Objekt und Callback wurden entfernt.
- Boot-Taster GPIO0 und Display/Audio bleiben erhalten.
- I2C GPIO15/16 bleibt fuer ES8311 und kuenftigen TCS34725 nutzbar.
- UART-Anschluss bleibt fuer GM865 reserviert.
- PN5180 ist noch nicht implementiert.

**Status:** Quellcode angepasst; noch nicht mit ESP-IDF kompiliert oder auf Hardware getestet.
