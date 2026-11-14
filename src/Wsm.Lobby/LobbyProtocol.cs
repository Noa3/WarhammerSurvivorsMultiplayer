using System.Net;

namespace Wsm.Lobby;

public static class LobbyProtocol
{
    public const int Version = 2;
}

public sealed class LobbyRequestException(string code, string message, HttpStatusCode? statusCode = null, Exception? inner = null)
    : HttpRequestException(message, inner, statusCode)
{
    public string Code { get; } = code;
}

internal sealed class LobbyRejection(int status, string code, string message) : Exception(message)
{
    public int Status { get; } = status;
    public string Code { get; } = code;
}
