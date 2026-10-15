HMB | TEC FlameEngine V1.4 – Magic Sparkle
Basis: V1.3 Mood Aura. Bestehende Flamme und Mood Aura bleiben unveraendert.

Dateien nach main/boards/hmbtec-dualeye/ kopieren:
  config.h, hmb_dualeye.cc, hmb_dualeye.h

Sterne: links X 48..82, rechts X 158..192, Y 47..80 (240x240).
Ein Ereignis alle 15..40 Sekunden, 1 oder gelegentlich 2 Sterne.
2. Stern erscheint 300 ms spaeter. Lebensdauer 520..800 ms.
Beide Displays verwenden denselben Framebuffer und bleiben synchron gespiegelt.
HMB_FLAME_SPARKLE_EN=0 deaktiviert den Effekt.

Build:
python3 scripts/build.py hmbtec-dualeye --name hmbtec-dualeye-4mb --language de-DE

Hinweis: Kein ESP-IDF-Build oder Hardwaretest in dieser Umgebung durchgefuehrt.
