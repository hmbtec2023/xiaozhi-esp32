HMB|TEC Persona Light V0.3.17 – MCP Light Scene / Stummmodus

Basis: funktionierende V0.3.16.
- Automatischer MCP-Kameraablauf unveraendert.
- Neues MCP-Tool self.hmbtec.persona.set_light_scene mit 6 numerischen Parametern.
- RGB/Helligkeit/Dynamik werden atomar gespeichert und am Display gezeigt.
- Lautstaerke beim Codec-Zugriff auf 0 gesetzt; Chat-Textlabel ausgeblendet.
- Server wird aufgefordert, keine Sprach-/Textantwort zu erzeugen.
- KEIN Art-Net-Versand in dieser Testversion! scene_applied=false.

ACHTUNG: Die Server-TTS-Erzeugung wird durch den Prompt nicht technisch deaktiviert.
Die Stummschaltung kann durch spaetere Lautstaerke-Aenderungen ueberschrieben werden.
Die geaenderten Quellen sind NICHT auf dem Zielboard kompiliert/getestet.

Test: Nach Personenerkennung serielle Meldung LIGHT_SCENE ... erwarten.
Falls die KI das Tool nicht aufruft, serverseitige Tool-Freigabe/Prompt pruefen.
