# HMB|TEC TalkCard – Projektdokument

**Dokumentversion:** 1.0  
**Firmwarestand:** V0.4.7 (Quellcodeauszug vom 08.10.2026)  
**Dokumentiert:** 30.10.2026  
**Plattform:** XiaoZhi / ESP32-S3 / ESP-IDF 6.1  
**Grundlage:** `hmbtec-talkcard 5.zip` (Dateien `hmbtec_talkcard_board.cc`, `hmb_talkcard_nfc.h`, `config.h`, `config.json`) sowie dokumentierte serielle Funktionstests der V0.4.6.

## 1. Projektziel

Die **HMB|TEC TalkCard** ist ein kompakter, sprachgesteuerter KI-Companion. Vier Ecktasten ermöglichen die Auswahl von 16 KI-Aktionen. Ein separater PTT-Taster steuert die Spracheingabe. NFC-Karten können personalisierte KI-Anfragen, Affirmationen und thematische Gespräche starten. Die Verarbeitung erfolgt über die XiaoZhi-Plattform und MCP-Werkzeuge.

## 2. Hardware und Pinbelegung

| Baugruppe | Signal | ESP32-S3 GPIO |
|---|---|---:|
| Mikrofon (I²S) | WS | 4 |
| Mikrofon (I²S) | BCLK/SCK | 5 |
| Mikrofon (I²S) | DIN | 6 |
| Lautsprecher (I²S) | DOUT | 7 |
| Lautsprecher (I²S) | BCLK | 15 |
| Lautsprecher (I²S) | LRCK/WS | 16 |
| Ecktaste K1 | Eingang | 11 |
| Ecktaste K2 | Eingang | 12 |
| Ecktaste K3 | Eingang | 13 |
| Ecktaste K4 | Eingang | 14 |
| PTT | Eingang | 44 |
| BOOT | Eingang | 0 |
| RGB-Statuspixel | Daten, 1 Pixel | 43 |
| Onboard-LED | Ausgang | 48 |
| MFRC522 | SS/CS | 33 |
| MFRC522 | SCK | 34 |
| MFRC522 | MOSI | 35 |
| MFRC522 | MISO | 36 |
| MFRC522 | RST | 37 |

**Audio:** Mikrofoneingang 16 kHz, Audioausgabe 24 kHz. Der L/R-Pin des I²S-Mikrofons ist laut `config.h` fest mit GND zu verbinden. Der MFRC522 nutzt `SPI2_HOST` mit 1 MHz. **GPIO 33–37 dürfen nicht anderweitig durch den gewählten Speicher-/Boardmodus belegt sein.**

## 3. Firmwarestruktur

| Datei | Funktion |
|---|---|
| `hmbtec_talkcard_board.cc` | Boardinitialisierung, Tasten, Statuspixel, NFC-Dispatcher, MCP-Werkzeuge, Variationsverwaltung |
| `hmb_talkcard_nfc.h` | MFRC522-Ansteuerung, Kartenerkennung und Lesen des Nutzdatenbereichs |
| `config.h` | GPIOs, Audio- und NFC-Konfiguration |
| `config.json` | Boardvarianten und Buildparameter |
| `application.cc` / `application.h` | XiaoZhi-Anwendungs- und Sitzungssteuerung; **nicht Bestandteil des beigefügten Quellcodeauszugs** |

Die Boardregistrierung erfolgt über `DECLARE_BOARD(HmbtecTalkCardBoard)`.

## 4. Bedienkonzept

### 4.1 Ecktasten und PTT

- **Bereitschaft:** Erste Ecktaste K1–K4 wählt eine Kategorie; zweite Ecktaste wählt eine Aktion.
- **Auswahl-Timeout:** 30 Sekunden (`HMB_TC_TIMEOUT_MS=30000`).
- **PTT:** Langer Druck (Konfiguration: 700 ms) startet die Spracheingabe; Loslassen stoppt sie.
- **Während eines NFC-Talk-Dialogs (V0.4.7):** Die erste Betätigung einer beliebigen Ecktaste beendet die NFC-Talk-Sitzung und wird **nicht** zugleich als neue Kategorienwahl verwendet.
- **Danach:** Die nächste Ecktastenbetätigung kann wieder eine Kategorie wählen.
- **BOOT:** Ruft außerhalb des Startzustands `ToggleChatState()` auf; beim Start kann die WLAN-Konfiguration aktiviert werden.

### 4.2 Kategorien und Aktionen (4 × 4)

| Kategorie | Taste 1 | Taste 2 | Taste 3 | Taste 4 |
|---|---|---|---|---|
| **K1 – Achtsamkeit/Psyche** | Affirmation | Weisheit | Achtsamkeitsimpuls | Stimmungs-Check |
| **K2 – Unterhaltung/Spaß** | Witz | Fun Fact | Nachrichten kompakt | Rätsel |
| **K3 – Alltag/Fokus** | Morgen-Briefing | Fokus | Gesundheits-Reminder | Feierabend |
| **K4 – Emotion/Nähe** | Kompliment | Einschlafhilfe | Ermutigung | Companion |

Die Kategorien werden durch das RGB-Pixel signalisiert: K1 Grün, K2 Orange, K3 Blau, K4 Pink; Bereitschaft ohne Auswahl: Pixel aus.

## 5. NFC-Konzept

### 5.1 Karten und Format

Der MFRC522 erkennt ISO14443A-Karten; die dokumentierten Funktionstests erfolgten mit NFC-Forum-Type-2-Tags (7-Byte-UID). Die Firmware dekodiert **NDEF-Textrecords** aus dem Type-2-TLV und übernimmt UTF-8-Text. Zusätzlich existieren ältere `HMBTC1`- und ASCII-Legacy-Pfade.

**Kommandosyntax:**

```text
cmd=<kommando> para0=<wert> para1=<wert> para2=<wert>
```

`para1` und `para2` sind optional. Der Parser akzeptiert auch `&` als Trenner und Prozentkodierung. Leerzeichen innerhalb von Werten bleiben erhalten, solange sie nicht unmittelbar vor einem erkannten nächsten Parameternamen stehen.

### 5.2 Kommandos

| Kommando | Beispiel | Wirkung |
|---|---|---|
| `affirmation` | `cmd=affirmation para0=bitte lichtblick` | Einmalige Affirmation über bestehende Aktion K1/K1 |
| `prompt` | `cmd=prompt para0=wetterbericht` | Einmalige KI-Anfrage, One-Shot-Modus |
| `talk` | `cmd=talk para0=Raumfahrt para1=Kinder para2=DE` | Thematisches, mehrstufiges KI-Gespräch |

Ein Tag wird bei anhaltender Anwesenheit nicht fortlaufend erneut ausgelöst; ein neuer Auslösevorgang erfolgt nach Kartenwechsel beziehungsweise erkannter Entfernung und erneutem Auflegen.

### 5.3 Trigger- und MCP-Architektur

**Wichtig:** Die Firmware sendet keine langen Texte als Wake-Word-Trigger. Stattdessen verwendet sie kurze IDs und stellt die Parameter über MCP bereit.

```text
NFC-Tag
  → MFRC522 / Type-2-TLV / NDEF
  → ParseNfcCommand()
  → DispatchNfcCommand()
  → kurzer Trigger (WakeWordInvoke)
  → XiaoZhi / MCP-Tool
  → KI-Antwort / TTS
```

| NFC-Funktion | Trigger | MCP-Werkzeug |
|---|---|---|
| Affirmation | `HMBTC_AFFIRMATION` | `self.hmbtec.talkcard.affirmation` |
| Prompt | `HMBTC_NFC_PROMPT` | `self.hmbtec.talkcard.nfc_prompt` |
| Talk | `HMBTC_NFC_TALK` | `self.hmbtec.talkcard.nfc_talk` |

Für die reguläre Kontextsteuerung steht außerdem `self.hmbtec.talkcard.get_context` bereit. NFC-Karteninhalte werden als Nutzereingaben behandelt, nicht als Systemanweisungen.

## 6. Dialogsteuerung V0.4.7

Die Datei `hmbtec_talkcard_board.cc` enthält den Status `nfc_talk_active_` und folgende Logik:

1. `cmd=talk` speichert den NFC-Kontext und setzt `nfc_talk_active_=true`.
2. `WakeWordInvoke("HMBTC_NFC_TALK", false)` startet den Dialog ohne One-Shot-Abschluss.
3. Die KI ruft `self.hmbtec.talkcard.nfc_talk` auf, erhält Thema und Parameter und eröffnet das Gespräch.
4. Die folgenden Nutzereingaben erfolgen über PTT; der KI-Dialogkontext bleibt serverseitig erhalten, solange die Sitzung besteht.
5. Eine beliebige Ecktaste prüft `nfc_talk_active_.exchange(false)`, leert den NFC-Kontext, setzt die Kategorienwahl zurück und ruft `Application::EndConversation()` auf.
6. Der Tastendruck wird danach beendet (`return`); die Ecktaste ist beim nächsten Druck wieder für die normale Auswahl verfügbar.

**Abgrenzung:** Die Boarddatei ruft `Application::EndConversation()` auf. Dessen Implementierung liegt außerhalb des beigefügten Quellcodeauszugs und wurde für dieses Dokument **nicht erneut verifiziert**. Ebenso ist die verbale Gesprächsbeendigung kein nachgewiesener eigener Firmware-Abbruchmechanismus.

## 7. MCP-Aktionen und Variationen

Die 16 Ecktastenaktionen sind als MCP-Werkzeuge registriert. Für mehrere Aktionen existieren thematische Variationslisten. Ein Shuffle-Bag vermeidet Wiederholungen innerhalb eines Durchlaufs; die Position wird im NVS-Namensraum `hmb_tc_var` gespeichert. Die derzeitige Datenversionskennung ist `1`.

Beispiele für Variationsgruppen: Affirmationen (8), Weisheiten (8), Witze (10), Achtsamkeitsimpulse (6). Nicht alle 16 Aktionen benötigen eine Variation.

## 8. Build-Konfiguration

`config.json` definiert zwei Varianten:

| Variante | Flash | Besonderheiten |
|---|---|---|
| `hmbtec-talkcard-16mb` | 16 MB | USB Serial/JTAG; Partitionstabelle `partitions/v2/16m.csv` |
| `hmbtec-talkcard-4mb` | 4 MB | Quad-PSRAM-Konfiguration; Partitionstabelle `partitions/v2/4m.csv` |

Beide Varianten aktivieren `CONFIG_HMB_PTT_ONLY_MIC=y`. Die konkrete Eignung der Variante hängt von der tatsächlichen Boardbestückung ab.

Build aus dem XiaoZhi-Projektverzeichnis, nach Auswahl der passenden Boardkonfiguration:

```bash
idf.py build
idf.py flash monitor
```

Ein vollständiger Projekt-Build kann nicht allein aus dem hier beigefügten Board-Unterverzeichnis ausgeführt werden.

## 9. Nachgewiesene Tests

Die folgenden Ergebnisse stammen aus den im Projektverlauf übermittelten **seriellen Logs für V0.4.6**:

| Test | Beobachtung | Bewertung |
|---|---|---|
| Type-2-Tag / NDEF | Drei unterschiedliche 7-Byte-UIDs, NDEF korrekt dekodiert | Bestätigt |
| `cmd=affirmation` | MCP-Aktion aufgerufen; Affirmation gesprochen; Rückkehr zu `idle` | Bestätigt |
| `cmd=prompt` | MCP-Kontext abgerufen; Wetterabfrage und Sprachausgabe; One-Shot-Ende | Bestätigt |
| `cmd=talk` | MCP-Kontext abgerufen; KI stellte Raumfahrt-Einstiegsfrage | Bestätigt |
| Dialogfortsetzung | PTT-Antwort erkannt; KI reagierte kontextbezogen und fragte weiter | Bestätigt |
| Ecktaste beendet Dialog (V0.4.7) | Im Quellcode vorgesehen | **Hardwaretest offen** |

**Beobachtete Warnung:** Einzelne MQTT-Audiopakete wurden als vertauscht bzw. dupliziert protokolliert. Ein bleibender Fehler ist aus dem Log nicht belegt; bei hörbaren Aussetzern gesondert prüfen.

## 10. Bekannte Grenzen und Prüfpunkte

1. **V0.4.7-Abbruch:** Prüfen, ob `Application::EndConversation()` im vollständigen Projekt den Audiokanal und den Gerätezustand zuverlässig zurücksetzt.
2. **NFC-Kontext:** Der Kontext liegt in einer gemeinsam genutzten Zeichenkette. Schnelle Kartenwechsel während laufender MCP-Abfragen können eine Zuordnung zum ursprünglichen Auftrag erschweren; spätere Versionen sollten Aufträge atomar verwalten.
3. **PTT-/Audiozustand:** In V0.4.6 war nach dem PTT-Loslassen kurz `idle` sichtbar, bevor die KI antwortete. Bei weiteren Tests auf saubere Übergänge achten.
4. **Verbal beenden:** Als mögliche Komfortfunktion diskutiert, aber kein expliziter technischer Abschluss durch Sprachkommando nachgewiesen.
5. **Hardware-/Buildtest:** Das beigefügte Archiv enthält nicht das gesamte XiaoZhi-Projekt; ein vollständiger ESP-IDF-Build wurde für diese Dokumentation nicht durchgeführt.

## 11. Versionshistorie

| Version | Schwerpunkt |
|---|---|
| V0.3.x | Vier Kategorien, 16 Aktionen, PTT, RGB-Rückmeldung |
| V0.4.0–V0.4.5 | NFC-Anbindung, Type-2-/NDEF-Verarbeitung, Kommando-Dispatcher |
| V0.4.6 | Kurze NFC-Trigger und MCP-Kontextübergabe; Funktionstests erfolgreich |
| **V0.4.7** | **Abbruch eines NFC-Talk-Dialogs über beliebige Ecktaste; im bereitgestellten Boardquellcode integriert** |

## 12. Projektstatus und nächster Prüfschritt

**Status:** V0.4.6 im dokumentierten Funktionsumfang erfolgreich getestet. V0.4.7 im vorliegenden Boardcode implementiert; abschließender Build und Hardwaretest des Ecktasten-Abbruchs stehen aus.

**Empfohlener Abnahmetest:** NFC-`talk` starten → erste KI-Frage abwarten → mit PTT antworten → beliebige Ecktaste drücken → kontrollieren, dass die Sitzung endet und die nächste Ecktastenbetätigung wieder eine Kategorie auswählt.

---

*HMB|TEC – Projektdokument, erstellt anhand des bereitgestellten Quellcodeauszugs und der dokumentierten Testprotokolle. Nicht nachgewiesene Funktionen sind ausdrücklich als offen gekennzeichnet.*
