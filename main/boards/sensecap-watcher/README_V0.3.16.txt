HMB|TEC Persona Light V0.3.16 – MCP Vision Trigger

Änderungen:
- Die bisherige direkte Capture/Explain-Analyse aus SemanticLightTask entfernt.
- 2 Sekunden bestätigte Personenerkennung im Idle-Zustand -> WakeWordInvoke("HMB_PERSONA_ANALYZE").
- Neues MCP-Werkzeug self.hmbtec.persona.analyze nach TalkCard-Vorbild.
- KI soll anschließend das bestehende self.camera.take_photo aufrufen.
- Entfernt: ALTER zusätzlicher Auto-Chat-Aufruf in sscma_camera.cc (ToggleChatState), der konkurrierend Zuhören auslöste.
- Rearm nach 10 Sekunden ohne Person, keine Art-Net-Ausgabe.
- SSCMA-Erkennung, Kameratreiber und PTT-Funktion bleiben sonst unverändert.

Installation: sensecap_watcher.cc, sscma_camera.cc, persona_version.h nach
main/boards/sensecap-watcher/ kopieren, idf.py build && idf.py flash.

Erwartete Logs:
V0.3.16 PERSON CONFIRMED -> HMB_PERSONA_ANALYZE
Wake word detected: HMB_PERSONA_ANALYZE
V0.3.16 MCP persona.analyze invoked by assistant (nur wenn Server Tool aufruft)

WICHTIG: Server-/LLM-Verhalten noch nicht getestet. Das Tool garantiert
nicht, dass der Server den synthetischen Trigger als Text versteht oder
self.camera.take_photo aufruft. Der Status MCP KI ANFRAGE ist kein
Nachweis für eine abgeschlossene Analyse.
Nicht kompiliert oder auf Hardware getestet.
