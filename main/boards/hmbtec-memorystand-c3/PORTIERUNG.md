# HMB|TEC MemoryStand C3 V0.6.0 – IR-Portierung (Quellstand, nicht getestet)

Basis: vom Nutzer bereitgestellte C3-V0.5.1-Dateien und ATOM-S3-IR-Referenz.

## Dateien
Kopiere `hmbtec_memorystand_c3_board.cc`, `ir_remote_controller.h`, `config.h` und `config.json` in `main/boards/hmbtec-memorystand-c3/`. Sichere den bisherigen Ordner zuvor. Die vorhandene Board-Registrierung in CMake/Kconfig bleibt bestehen.

## Funktionen
- IR RX GPIO3: NEC/Extended-NEC, speichert Farbwahl; FLASH 0x4D startet HMBPROMPT.
- IR TX GPIO10: 38 kHz, stellt nach FLASH mit 500 ms Verzögerung zuletzt gewählte Farbe wieder her.
- MCP `self.light.set_color`: Farbe per Sprache/LLM setzen.
- MCP `self.hmbtec.get_affirmation`: farbbezogener Affirmationsprompt.
- Bestehende OLED/I2S/GPIO9-Funktionen unverändert.

## Nicht übernommen / Grenzen
- ATOM-S3-LD2410-Radar, Statuspixel und radarabhängige Automatik sind nicht Bestandteil dieser IR-Stufe: Hardware und Zustandsautomat auf C3 noch nicht integriert.
- GPIO10 braucht einen geeigneten IR-LED-Treiber; IR-Empfänger muss 3,3-V-kompatibel sein.
- RMT TX/RX-Ressourcen des ESP32-C3 sowie MCP-Registrierung und WakeWordInvoke ohne Wakeword-Engine müssen im Build und auf dem Gerät verifiziert werden.
- Code ist **nicht kompiliert oder auf Hardware getestet**.

## Build
`python3 scripts/build.py hmbtec-memorystand-c3 --name hmbtec-memorystand-c3 --language de-DE`
