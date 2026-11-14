using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;

namespace Wsm.Lobby;

public sealed class LobbyClient : IAsyncDisposable
{
    private readonly HttpClient http;
    private bool left;
    public Guid PlayerId { get; }
    private LobbyClient(HttpClient http, Guid id) { this.http = http; PlayerId = id; }
    public static async Task<LobbyClient> JoinAsync(Uri address, string name, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(address);
        if (!address.IsAbsoluteUri || address.Scheme != Uri.UriSchemeHttp) throw new ArgumentException("Expected an absolute HTTP lobby address.", nameof(address));
        var http = new HttpClient { BaseAddress = address, Timeout = TimeSpan.FromSeconds(10), MaxResponseContentBufferSize = 32768 };
        try {
            using var response = await http.PostAsJsonAsync("/join", new JoinRequest(name, LobbyProtocol.Version), cancellationToken); await Check(response, cancellationToken);
            var joined = await ReadJson<JoinResponse>(response, cancellationToken);
            if (joined.ProtocolVersion != LobbyProtocol.Version) throw new LobbyRequestException("protocol_mismatch", "Lobby protocol versions differ.");
            if (joined.PlayerId == Guid.Empty || joined.Token is not { Length: 64 } || !joined.Token.All(Uri.IsHexDigit))
                throw new LobbyRequestException("incompatible_server", "Invalid lobby welcome response.");
            http.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", joined.Token);
            return new(http, joined.PlayerId);
        } catch { http.Dispose(); throw; }
    }
    public async Task<LobbySnapshot> GetStateAsync(CancellationToken cancellationToken = default)
    {
        using var response = await http.GetAsync("/state", cancellationToken); await Check(response, cancellationToken);
        var state = await ReadJson<LobbySnapshot>(response, cancellationToken);
        if (state.Rules is null || state.Slots is null || state.Slots.Count is < 1 or > 4 ||
            state.Slots.Any(s => s is null || s.WeaponIds is null) || !state.Slots.Any(s => s.PlayerId == PlayerId))
            throw new LobbyRequestException("incompatible_server", "Invalid lobby state response.");
        return state;
    }
    public Task SetLoadoutAsync(string heroId, IEnumerable<string> weaponIds, CancellationToken cancellationToken = default) => Post("/loadout", new LoadoutRequest(heroId, weaponIds.ToArray()), cancellationToken);
    public Task SetReadyAsync(bool ready, CancellationToken cancellationToken = default) => Post("/ready", new ReadyRequest(ready), cancellationToken);
    public Task HeartbeatAsync(CancellationToken cancellationToken = default) => Post("/heartbeat", new { }, cancellationToken);
    public async Task LeaveAsync(CancellationToken cancellationToken = default)
    {
        if (left) return;
        try { await Post("/leave", new { }, cancellationToken); }
        catch (HttpRequestException ex) when (ex.StatusCode == System.Net.HttpStatusCode.Unauthorized) { /* Expired or already removed. */ }
        left = true;
    }
    private async Task Post<T>(string path, T body, CancellationToken ct) { using var response = await http.PostAsJsonAsync(path, body, ct); await Check(response, ct); }
    private static async Task<T> ReadJson<T>(HttpResponseMessage response, CancellationToken ct)
    {
        try { return await response.Content.ReadFromJsonAsync<T>(ct) ?? throw new JsonException("Empty response."); }
        catch (Exception ex) when (ex is JsonException or NotSupportedException)
        { throw new LobbyRequestException("incompatible_server", "The server did not return a valid lobby response.", response.StatusCode, ex); }
    }
    private static async Task Check(HttpResponseMessage response, CancellationToken ct)
    {
        if (response.IsSuccessStatusCode) return;
        string code = response.StatusCode switch
        {
            System.Net.HttpStatusCode.Unauthorized => "session_expired",
            System.Net.HttpStatusCode.UpgradeRequired => "protocol_mismatch",
            _ => "request_failed"
        };
        try
        {
            var error = await response.Content.ReadFromJsonAsync<ErrorResponse>(ct);
            if (error?.Code is "lobby_full" or "lobby_unavailable" or "protocol_mismatch" or "session_expired" or "invalid_request" or "state_conflict") code = error.Code;
        }
        catch (Exception ex) when (ex is JsonException or NotSupportedException) { /* The HTTP status remains useful without a structured body. */ }
        // Do not reflect arbitrary remote response bodies or admission tokens into the UI/logs.
        throw new LobbyRequestException(code, $"Lobby request failed (HTTP {(int)response.StatusCode}, {code}).", response.StatusCode);
    }
    public async ValueTask DisposeAsync() { try { if (!left) await LeaveAsync(); } finally { http.Dispose(); } }
}
