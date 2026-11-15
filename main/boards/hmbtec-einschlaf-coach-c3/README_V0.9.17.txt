HMB|TEC Sleep Companion C3 V0.9.17

- GPIO3 Deep-Sleep-Wakeup auf EXT1 ANY_LOW korrigiert (ESP32-C3).
- Doppelte/ungeeignete gpio_wakeup_enable und esp_sleep_enable_gpio_wakeup Aufrufe entfernt.
- Status-NeoPixel-Task wird vor dem letzten LED-Clear angehalten.
- SSD1306 wird vor Deep Sleep per Display OFF dunkel geschaltet.
- Sound DEEP_SLEEP.ogg ist weiterhin deaktiviert: Die hochgeladene ZIP enthaelt weder OGG-Datei noch CMakeLists.txt. Fuer Einbettung: DEEP_SLEEP.ogg dem bestehenden EMBED_FILES der Projekt-CMakeLists.txt hinzufuegen und HMB_DEEP_SLEEP_SOUND_EMBEDDED auf 1 setzen.
- Nicht auf Hardware kompiliert/getestet.

Hinweis: Der externe Pull-up an GPIO3 ist fuer sicheres Wake-up bei abgeschalteten RTC-Power-Domaenen empfehlenswert.
