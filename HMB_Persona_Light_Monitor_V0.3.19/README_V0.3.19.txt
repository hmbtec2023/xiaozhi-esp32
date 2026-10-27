HMB|TEC Persona Light V0.3.19 – MONITOR-ONLY

Basis: funktionierender UI-Patch V0.3.18.
Geaendert: sensecap_watcher.cc und persona_version.h.
NICHT geaendert: application.cc, sscma_camera.cc, MCP-Ablauf, Kamerasteuerung.

Die Displayanzeige erhaelt eine letzte Zeile: KI:<IDLE|CONNECT|LISTEN|SPEAK|NOTIFY|OTHER> MCP:+<Sekunden>s.
Die Zeit startet beim Eingang der MCP-Lichtparameter. Zustandswechsel werden auch via ESP_LOGI protokolliert, ein serieller Monitor ist nicht erforderlich.

HMB_PERSONA_FINISH_MODE steht absichtlich auf MONITOR. AUTO ist NICHT implementiert und darf NICHT aktiviert werden.

Vor dem Einspielen aktuelle V0.3.18 sichern. Im Projektroot entpacken, Build pruefen und erst dann flashen.
Die Firmware wurde hier NICHT mit ESP-IDF gebaut oder auf Hardware getestet.
