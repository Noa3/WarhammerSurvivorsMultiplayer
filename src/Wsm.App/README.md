# Windows-Lobby

Starten (Windows mit .NET 10 Desktop- und ASP.NET-Core-Runtime bzw. SDK):

```text
dotnet run --project src/Wsm.App/Wsm.App.csproj
```

Eine Person erstellt die Lobby und wählt ausdrücklich die Bindung: standardmäßig nur Loopback; LAN-Bindung an alle IPv4-Schnittstellen nur in einem vertrauenswürdigen Netzwerk. Für LAN eine angezeigte erreichbare IPv4-Adresse und den Port separat weitergeben. Die Vorschläge garantieren keine Erreichbarkeit; es werden keine Firewall-Regeln geändert. Gäste geben nur die IP und den Port ein. HTTP ist unverschlüsselt, ohne Passwort und nicht für das öffentliche Internet geeignet.

Jede Person trägt eigene Helden- und 1–8 Waffen-IDs ein und übernimmt diese vor „Bereit“. Die IDs sind nicht gegen das Spiel geprüft. Die Lobby unterstützt insgesamt vier Plätze einschließlich Host und Bots. Änderungen an Regeln und manche Teilnehmerwechsel setzen Bereitschaft zurück; der Server weist Kapazitätskonflikte ab. Ein Gast muss nach Sitzungsablauf neu beitreten.

**Entwicklungsstand:** Es gibt weder einen verifizierten Spielkatalog noch eine Spielanbindung. Startanfragen werden vom Lobby-Server mit Begründung abgelehnt. Es wird kein Spiel gestartet und kein Spielstand verändert.

Der echte WinForms-/Loopback-Integrationstest verwendet ausdrücklich synthetische IDs:

```text
dotnet run --project tests/Wsm.App.Smoke/Wsm.App.Smoke.csproj
```
