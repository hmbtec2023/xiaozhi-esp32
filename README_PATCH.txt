HMB|TEC MemoryStand C3 V0.4.1 – Buildregistrierung

Basis: vom Nutzer bereitgestelltes main(4).zip (September 2026).
Enthaelt vollstaendige main/Kconfig.projbuild und main/CMakeLists.txt mit zusaetzlicher Boardregistrierung sowie Boarddateien.
Vor dem Entpacken eigene main/Kconfig.projbuild und main/CMakeLists.txt sichern! Diese Dateien ersetzen die jeweiligen Dateien der gelieferten Firmwareversion und duerfen nicht blind in andere Versionen kopiert werden.
Ins Projektroot entpacken, sodass main/... erhalten bleibt.
Danach: idf.py set-target esp32c3; idf.py menuconfig -> Board Type -> HMB|TEC MemoryStand C3.
HINWEIS: Build noch nicht ausgefuehrt. Die 24-kHz-Duplex-Taktung und Audioqualität sind noch zu validieren.
