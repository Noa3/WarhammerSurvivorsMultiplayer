using System.Net;
using System.Net.Sockets;
using Wsm.Lobby;

namespace Wsm.App;

public static class ConnectionFeedback
{
    public static string Describe(Exception error)
    {
        if (error is AggregateException aggregate && aggregate.InnerExceptions.Count > 0)
            return Describe(aggregate.InnerExceptions[0]) + " Auch beim Aufräumen ist ein Fehler aufgetreten.";
        if (error is LobbyRequestException lobby)
        {
            var reason = lobby.Code switch
            {
                "lobby_full" => "Die Lobby ist voll. Der Host kann einen Bot-Platz freigeben oder auf einen freien Spielerplatz warten.",
                "lobby_unavailable" => "Die Partie wird bereits gestartet oder läuft schon. Ein späterer Beitritt ist noch nicht verfügbar.",
                "protocol_mismatch" => "Unterschiedliche Lobby-Versionen. Host und Spieler müssen denselben aktuellen Build der Anwendung verwenden.",
                "session_expired" => "Die Lobby-Sitzung ist nicht mehr gültig. Bitte trennen und erneut beitreten. Run-Reconnect ist noch nicht implementiert.",
                "incompatible_server" => "Die Gegenstelle antwortet nicht als kompatibler Lobbyserver. IP, Port und Version der Anwendung prüfen.",
                "invalid_request" => "Die Lobby hat die Eingabe abgelehnt. Spielernamen und Auswahl prüfen; Namen dürfen 1–64 Zeichen enthalten.",
                "state_conflict" => "Diese Aktion ist im aktuellen Lobby-Zustand nicht möglich. Auswahl und Bereitschaft prüfen und die Lobby aktualisieren.",
                _ => "Der Lobbyserver hat die Anfrage abgelehnt. Beim Host nachsehen und erneut versuchen."
            };
            return reason + (lobby.StatusCode is { } status ? $" (HTTP {(int)status})" : "");
        }
        if (error is OperationCanceledException or TimeoutException)
            return "Zeitüberschreitung: Der Host hat nicht rechtzeitig geantwortet. IP, Port, LAN/VPN und beim Host die LAN-Bindung prüfen. Eine Firewall ist eine mögliche, nicht nachgewiesene Ursache.";
        for (Exception? nested = error; nested is not null; nested = nested.InnerException)
        {
            if (nested is not SocketException socket) continue;
            return socket.SocketErrorCode switch
            {
                SocketError.ConnectionRefused => "Keine Lobby erreichbar: Die Verbindung zu diesem Port wurde abgelehnt. Der Host muss die Lobby erstellen; IP, Port und LAN-Bindung prüfen.",
                SocketError.NetworkUnreachable or SocketError.HostUnreachable or SocketError.HostNotFound => "Host oder Netzwerk nicht erreichbar. IP-Adresse und die gemeinsame LAN/VPN-Verbindung prüfen.",
                SocketError.TimedOut => "Zeitüberschreitung beim Verbindungsaufbau. Host, Port, LAN/VPN und mögliche Firewall-Regeln prüfen.",
                SocketError.ConnectionReset or SocketError.ConnectionAborted => "Die Verbindung zum Host wurde unterbrochen. Der Host könnte die Lobby geschlossen haben; Netzwerk prüfen und erneut versuchen.",
                SocketError.AddressAlreadyInUse => "Der gewählte Port ist bereits belegt. Eine andere Portnummer wählen oder die andere Lobby schließen.",
                SocketError.AccessDenied => "Das Betriebssystem hat den Netzwerkzugriff verweigert. Lokale Netzwerkberechtigungen prüfen.",
                _ => $"Netzwerkfehler ({socket.SocketErrorCode}). IP, Port und LAN/VPN prüfen."
            };
        }
        if (error is HttpRequestException http)
            return http.StatusCode is { } status
                ? $"Der Server antwortet mit HTTP {(int)status}. IP, Port und Lobbyserver prüfen."
                : "Verbindungsaufbau fehlgeschlagen. IP, Port, LAN/VPN und die Lobby beim Host prüfen; die genaue Ursache ist nicht eindeutig erkennbar.";
        if (error is ArgumentException) return "Ungültige Eingabe: " + error.Message;
        return "Aktion fehlgeschlagen: " + error.Message;
    }
}
