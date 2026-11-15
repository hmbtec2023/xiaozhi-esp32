HMB|TEC Sleep Companion C3 V0.9.15 – experimenteller Quellcode

- Shortpress: P1/P2/P3
- Nach 1 s Halten: CHAT_START.ogg; Loslassen vor 4 s: freies KI-Gespraech
- Nach 4 s Halten: DEEP_SLEEP.ogg; Loslassen: Deep Sleep
- Nach 5 min durchgehend IDLE: Deep Sleep ohne Sound
- GPIO3 LOW: Deep-Sleep-Wakeup; nach Wakeup kompletter Neustart
- RTC_DATA_ATTR: P-Zaehler bleibt ueber Deep Sleep erhalten.

WICHTIG:
1. DEEP_SLEEP.ogg war NICHT im hochgeladenen ZIP enthalten. Die Datei muss
   in die XiaoZhi-Assets/CMake-EMBED_FILES aufgenommen werden. Erst dann
   HMB_DEEP_SLEEP_SOUND_EMBEDDED auf 1 setzen. Sonst erfolgt Very-Longpress
   ohne Deep-Sleep-Sound.
2. HMB_SLEEP_SOUND_WAIT_MS auf die reale Sounddauer abstimmen. PlaySound
   liefert hier keine bestaetigte Playback-Ende-Rueckmeldung.
3. GPIO3 war im hochgeladenen config.h zugleich IR_RX_GPIO; IR-Empfang wurde
   deshalb im Board-Konstruktor deaktiviert. Taster und IR-RX duerfen
   nicht auf demselben GPIO betrieben werden.
4. Das Booten nach Deep Sleep dauert bis WLAN/XiaoZhi bereit sind. Der
   Wakeup-Tastendruck wird nicht als P1/P2/P3 ausgewertet.
5. 8-h-Sitzungsreset nach Deep Sleep erfordert gueltige Systemzeit. Ohne
   Zeitsynchronisation bleibt der RTC-P-Zaehler erhalten, auch wenn 8 h
   vergangen sind. Stromverlust setzt RTC-Daten zurueck.
6. NICHT kompiliert / NICHT auf Hardware getestet. Bitte zuerst sichern.
7. Die Tastensteuerung nutzt jetzt GPIO-Polling statt Button::OnClick/
   OnLongPress, damit die beiden Tonschwellen vor dem Loslassen feuern.
