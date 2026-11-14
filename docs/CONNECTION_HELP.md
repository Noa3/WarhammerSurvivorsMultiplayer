# Connection troubleshooting

The Windows lobby shows the attempted IPv4 address and port, a German failure reason, and a suggested next action. A pending attempt displays `Verbindung wird aufgebaut …`; controls prevent duplicate attempts. A failed join leaves the Join button available and does not advertise readiness or a player roster. If the server accepted a join but the first state fetch failed, the client attempts to leave that slot before allowing a fresh join.

| Message | What is known | Suggested action |
| --- | --- | --- |
| Keine Lobby erreichbar | The connection to the selected port was refused. | The host must create the lobby. Check the IP, port and host binding. |
| Zeitüberschreitung | No successful response arrived before the deadline. | Check host availability, LAN/VPN, address and port. A firewall is possible, not diagnosed. |
| Host oder Netzwerk nicht erreichbar | The operating system reported an unreachable network/host. | Check the IP and LAN/VPN connection. |
| Die Lobby ist voll | The host rejected admission because all human/bot slots are occupied. | Ask the host to free a bot slot or wait for a human slot. |
| Unterschiedliche Lobby-Versionen | The lobby protocol versions do not match. | Use the same current application build on both machines. This does not validate the installed game's version. |
| Die Partie wird bereits gestartet oder läuft schon | The host is launching or has started the session. | Late joining is not implemented. |
| Die Lobby-Sitzung ist nicht mehr gültig | The admission token is no longer accepted. | Disconnect and join again. This is a fresh lobby join, not restoration of an in-game run. |
| Kein kompatibler Lobbyserver | The response is malformed or does not satisfy the lobby handshake. | Check that the selected port belongs to this application, not another web service. |
| Der gewählte Port ist bereits belegt | Another local listener uses the requested host port. | Choose another port or close the other lobby. |

The default is loopback, reachable only on the host's PC. To admit a friend over a trusted LAN/VPN, the host must select the explicit LAN binding, share a reachable IPv4 address and the same port (default 7780), and permit the connection through their own network configuration if necessary. The application does not change firewall rules or routers, and does not diagnose a firewall merely from a timeout.

Current lobby protocol version: **2**. Missing versions are refused before a player slot is created; typed rejections preserve HTTP status (for example 426 for protocol mismatch, 409 for a full/started lobby and 401 for an invalid session). Arbitrary remote error bodies and admission tokens are not copied into displayed network errors.

The join HTTP deadline is 10 seconds; live UI refresh requests have a shorter 4-second deadline. Caller cancellation during form shutdown is not presented as a connection failure. GUI regression tests exercise an actual refused loopback port, an actual full lobby and an accepted HTTP connection that times out. Network tests also cover protocol mismatch, invalid session and malformed welcome responses.

These diagnostics concern the working lobby/control channel. They do not imply a functioning game adapter, automatic run reconnection, host migration or safe public-internet hosting. Use a trusted LAN or secured VPN only; HTTP is not encrypted.
