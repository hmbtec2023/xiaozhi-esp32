#!/usr/bin/env python3
"""HMB|TEC Persona Light V0.3.18 – patch current application.cc in place."""
import sys
from pathlib import Path
p=Path(sys.argv[1]); s=p.read_text()
replacements={
 'display->SetStatus(Lang::Strings::STANDBY);':'display->SetStatus("BEREIT");',
 'display->SetStatus(Lang::Strings::CONNECTING);':'display->SetStatus("KI VERBINDET");',
 'display->SetStatus(Lang::Strings::LISTENING);':'display->SetStatus("KI ANALYSIERT");',
 'display->SetStatus(Lang::Strings::SPEAKING);':'display->SetStatus("LICHTSZENE AKTIV");',
}
# Only replace in HandleStateChangedEvent, not in other functions.
a=s.index('void Application::HandleStateChangedEvent() {')
b=s.index('void Application::ShowStartupWakeWordHint()',a)
section=s[a:b]
for old,new in replacements.items():
 if old not in section: raise SystemExit('Abbruch: Erwarteter Status fehlt: '+old)
 section=section.replace(old,new)
s=s[:a]+section+s[b:]
p.write_text(s)
print('Persona Light V0.3.18: vier Zustandsanzeigen angepasst.')
