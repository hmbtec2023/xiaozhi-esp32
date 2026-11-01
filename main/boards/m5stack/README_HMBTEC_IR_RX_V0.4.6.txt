HMB|TEC MemoryStand V0.4.6 – IR-Empfang (Diagnose)

Basis: vom Nutzer bereitgestelltes atoms3-memorystand 2.zip (V0.4.5).
Referenz: hmbtec-lichtblick-bread 3.zip / ir_remote_controller.h.

Neu: externer demodulierender 38-kHz-IR-Empfaenger via RMT RX auf GPIO2.
GPIO2 ist ein VORSCHLAG, nicht hardwareseitig bestaetigt. Pin und Modulversorgung pruefen.
GPIO4 bleibt IR-TX. Radar GPIO38/39 und Statuspixel GPIO1 bleiben unveraendert.
NEC-8 und Extended NEC (16-Bit-Adresse) werden im Log ausgewiesen.
Nur Diagnose: empfangene Befehle loesen bewusst keine Aktionen aus.
Die Referenzbibliothek liefert im Callback nur das Kommando (8 Bit); die
vollstaendige Adresse erscheint im Log, nicht im Callback.

Installation: Beide Dateien in
  ~/Documents/XIAOZHI/main/boards/m5stack/atoms3-memorystand/
kopieren. Keine Aenderung an AromaEngine erforderlich.

Build (in derselben Terminal-Sitzung):
  source ~/.espressif/v6.1/esp-idf/export.sh
  cd ~/Documents/XIAOZHI && python3 scripts/build.py m5stack/atoms3-memorystand --name atoms3-memorystand --language de-DE

Noch nicht auf Hardware kompiliert/getestet.
