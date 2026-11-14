namespace Wsm.InGame.Runtime;

/// <summary>Lifecycle of the in-game session owned by this process.</summary>
public enum IngameState
{
    Disconnected = 0,
    Hosting = 1,
    Connecting = 2,
    Connected = 3,
    Failed = 4,
}

/// <summary>Actionable failure category. Never carries remote-controlled text.</summary>
public enum IngameFailure
{
    None = 0,
    Refused,
    Timeout,
    Rejected,
    HostFull,
    HostUnreachable,
    HostLost,
    PortInUse,
    Busy,
    InvalidArgument,
    Internal,
}

/// <summary>Immutable, thread-safe view of the lobby connection. UI reads it by polling.</summary>
public sealed record IngameStatus(
    IngameState State,
    IngameFailure Failure,
    int PeerCount,
    int BoundPort,
    string Endpoint,
    string Text)
{
    public static readonly IngameStatus Idle = new(IngameState.Disconnected, IngameFailure.None, 0, 0, "", "Nicht verbunden.");

    /// <summary>Stable ASCII category for native consumers; empty when no failure is pending.</summary>
    public string FailureCode => Failure switch
    {
        IngameFailure.None => "",
        IngameFailure.Refused => "refused",
        IngameFailure.Timeout => "timeout",
        IngameFailure.Rejected => "rejected",
        IngameFailure.HostFull => "hostfull",
        IngameFailure.HostUnreachable => "unreachable",
        IngameFailure.HostLost => "hostlost",
        IngameFailure.PortInUse => "portinuse",
        IngameFailure.Busy => "busy",
        IngameFailure.InvalidArgument => "invalid",
        _ => "internal",
    };
}

/// <summary>German player-facing text. Only constant phrases, never remote payloads or tokens.</summary>
internal static class IngameStatusText
{
    public const int MaxBytes = 4096;

    public static string Describe(IngameState state, IngameFailure failure, int peers, int boundPort, string endpoint, bool lan) => state switch
    {
        IngameState.Hosting => lan
            ? $"Host aktiv auf Port {boundPort} im lokalen Netzwerk. Verbunden: {peers} von 3 Mitspielern."
            : $"Host aktiv auf Port {boundPort} nur auf diesem PC. Verbunden: {peers} von 3 Mitspielern.",
        IngameState.Connecting => $"Verbinde mit {endpoint} ...",
        IngameState.Connected => $"Verbunden mit {endpoint}.",
        IngameState.Disconnected => failure switch
        {
            IngameFailure.HostLost => "Verbindung zum Host verloren. Der Host hat das Spiel beendet oder die Verbindung ist abgebrochen.",
            _ => "Nicht verbunden.",
        },
        _ => failure switch
        {
            IngameFailure.Refused => "Host nicht erreichbar: Verbindung abgelehnt. Prüfen Sie die IP-Adresse und ob der Host die Lobby gestartet hat.",
            IngameFailure.Timeout => "Zeitüberschreitung beim Verbindungsaufbau. Prüfen Sie Netzwerk und Firewall des Hosts.",
            IngameFailure.Rejected => "Der Host hat den Beitritt abgelehnt. Prüfen Sie Spielversion und Passwort.",
            IngameFailure.HostFull => "Der Host ist voll oder hat die Verbindung sofort geschlossen. Versuchen Sie es später erneut.",
            IngameFailure.HostUnreachable => "Netzwerk nicht erreichbar. Prüfen Sie die IPv4-Adresse im lokalen Netzwerk.",
            IngameFailure.PortInUse => "Der Port ist bereits belegt. Wählen Sie einen anderen Port.",
            IngameFailure.Busy => "Es läuft bereits eine Verbindung. Verlassen Sie zuerst die aktuelle Lobby.",
            IngameFailure.InvalidArgument => "Ungültige Eingabe: erlaubt ist eine IPv4-Adresse und ein Port von 1 bis 65535.",
            IngameFailure.Internal => "Interner Netzwerkfehler. Versuchen Sie es erneut.",
            _ => "Nicht verbunden.",
        },
    };

    /// <summary>UTF-8 bytes of the text, hard-capped at <see cref="MaxBytes"/> including the terminator.</summary>
    public static byte[] ToBoundedUtf8(string text)
    {
        var bytes = System.Text.Encoding.UTF8.GetBytes(text);
        if (bytes.Length <= MaxBytes - 1) return bytes;
        var limit = MaxBytes - 1;
        while (limit > 0 && (bytes[limit] & 0xC0) == 0x80) limit--; // do not split a UTF-8 sequence
        var truncated = new byte[limit];
        Array.Copy(bytes, truncated, limit);
        return truncated;
    }
}
