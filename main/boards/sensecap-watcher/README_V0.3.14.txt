HMB|TEC Persona Light V0.3.14 — Semantic Light Test

Aenderung gegenueber V0.3.13:
- Die automatische semantische Bildanalyse wird nicht mehr durch kDeviceStateIdle blockiert.
- Bei bestaetigter Personenerkennung (2 s) startet Capture() -> Explain() auch dann, wenn XiaoZhi "Zuhoeren" anzeigt.
- Kein automatischer Aufruf von ToggleChatState() oder WakeWordInvoke() durch SemanticLightTask.
- Die PTT-Tastenfunktion, SSCMA-Inferenz und Kamera-Synchronisierung sind unveraendert.
- Erneute Ausloesung erst nach 10 s ohne Personenerkennung.
- Keine Art-Net-Ausgabe, keine automatische Begruessung.

Installation:
Die Dateien sensecap_watcher.cc und persona_version.h nach
main/boards/sensecap-watcher/ kopieren.
idf.py build && idf.py flash

Wichtig:
- Nur Quellcode-Patch, kein vollstaendiges Firmwareprojekt.
- Nicht kompiliert und nicht auf Hardware getestet.
- Die automatische Analyse erfordert eine vom XiaoZhi-Server bereitgestellte Vision-URL
  (McpServer::ParseCapabilities). Ohne URL zeigt die Firmware ANALYSEFEHLER.
- Bei weiterhin angezeigtem "Zuhoeren" ist die XiaoZhi-Sprachzustandsanzeige
  unabhaengig von der semantischen Analyse; pruefe die Persona-Statuszeile.
