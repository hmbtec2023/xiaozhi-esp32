HMB|TEC Einschlaf-Coach C3 V0.9.20 - Ansage-Testmodus

#define HMB_ANSAGE_TEST_MODE 1
GPIO3: bei jedem Loslassen P1, P2, P3 zyklisch anfordern.
Board-Tasten-State-Machine, Long-Press und Deep-Sleep-Automatik sind im Testpfad inaktiv.

WICHTIGE GRENZE:
Die P1/P2/P3-Ansagen werden ueber WakeWordInvoke und damit ueber die
XiaoZhi-Anwendungs-State-Machine angefordert. Der Board-Testpfad startet
selbst keine Spracheingabe, kann aber die Mikrofonaktivierung durch den
XiaoZhi AudioService NICHT unterbinden. Fuer eine echte, garantierte
Mikrofonsperre muss der AudioService bzw. die Application angepasst werden.
Die drei Ansagen muessen serverseitig fuer HMBSLEEP1..3 konfiguriert sein.

Nicht kompiliert oder auf Hardware getestet.
