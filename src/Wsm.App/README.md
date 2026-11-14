# Windows-Lobby

Starten (Windows mit .NET 10 Desktop- und ASP.NET-Core-Runtime bzw. SDK):

```text
dotnet run --project src/Wsm.App/Wsm.App.csproj
```

Eine Person erstellt die Lobby und wählt ausdrücklich die Bindung: standardmäßig nur Loopback; LAN-Bindung an alle IPv4-Schnittstellen nur in einem vertrauenswürdigen Netzwerk. Für LAN eine angezeigte erreichbare IPv4-Adresse und den Port separat weitergeben. Die Vorschläge garantieren keine Erreichbarkeit; es werden keine Firewall-Regeln geändert. Gäste geben nur die IP und den Port ein. HTTP ist unverschlüsselt, ohne Passwort und nicht für das öffentliche Internet geeignet.

Jede Person trägt eigene Helden- und 1–8 Waffen-IDs ein und übernimmt diese vor „Bereit“. Die IDs sind nicht gegen das Spiel geprüft. Die Lobby unterstützt insgesamt vier Plätze einschließlich Host und Bots. Änderungen an Regeln und manche Teilnehmerwechsel setzen Bereitschaft zurück; der Server weist Kapazitätskonflikte ab. Ein Gast muss nach Sitzungsablauf neu beitreten.

**Entwicklungsstand:** Es gibt weder einen verifizierten Spielkatalog noch eine Gameplay- oder Kampagnenanbindung. Koop-Startanfragen werden vom Lobby-Server mit Begründung abgelehnt. Optional kann über „Spiel separat starten (experimentell)“ nach ausdrücklicher Bestätigung eine separate Kopie der ausgewählten Installation im Fenstermodus gestartet werden. Diese Kopie isoliert weder Spielstände noch Benutzerprofil oder Steam Cloud; das Spiel kann die üblichen Daten lesen und verändern. Die Lobby bleibt als separates Fenster erreichbar. Eine Ingame-Leiste wird nicht ausgeliefert, weil die sichtbare Darstellung mit Unity noch nicht zuverlässig funktioniert. Ein gestartetes Spiel ist noch kein gemeinsames Spiel.

Der echte WinForms-/Loopback-Integrationstest verwendet ausdrücklich synthetische IDs:

```text
dotnet run --project tests/Wsm.App.Smoke/Wsm.App.Smoke.csproj
```
