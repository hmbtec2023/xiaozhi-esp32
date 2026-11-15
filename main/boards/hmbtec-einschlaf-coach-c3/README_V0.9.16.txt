HMB|TEC Sleep Companion C3 V0.9.16

Änderungen gegenüber V0.9.15:
- OLED wird vor Deep Sleep per esp_lcd_panel_disp_on_off(panel_, false) dunkelgeschaltet.
- NeoPixel wird weiterhin gelöscht; Rückgabewert wird geprüft.
- Wichtige Statusmeldungen mit UTF-8-Icons versehen.
- Periodische Log-Ausgabe mit Tag SystemInfo unterdrückt (esp_log_level_set).
- Versionsmeldung aktualisiert.

Hinweis: OLED, INMP441 und MAX98357A bleiben elektrisch versorgt, sofern keine externe Versorgungsschaltung vorhanden ist.
Die SystemInfo-Task selbst wird nicht deaktiviert, nur ihre Logausgabe.
Bei ESP-IDF-Konfiguration mit Log-Level-Filterung zur Compilezeit kann die Unterdrückung anders ausfallen.
Nicht kompiliert / nicht auf Hardware getestet.
