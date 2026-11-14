using System.Buffers.Binary;
using System.Text.Json;
using System.Net.Sockets;

namespace Wsm.Core;

internal sealed record Message(string Type, int Version = 0, string? Password = null, Guid PlayerId = default, SessionSettings? Settings = null, PlayerInput? Input = null, UpgradeChoice? Upgrade = null, WorldSnapshot? Snapshot = null, string? Error = null, RewardReceipt? Reward = null, Guid Nonce = default);

internal sealed class Peer : IDisposable
{
    internal const int MaxFrame = 262144;
    private static readonly TimeSpan Timeout = TimeSpan.FromSeconds(30);
    private readonly SemaphoreSlim sendLock = new(1, 1);
    private readonly Stream stream;
    private readonly Action close;
    private int disposed;
    private readonly object heartbeatGate = new();
    private CancellationTokenSource? heartbeatStop;
    private Task heartbeatTask = Task.CompletedTask;
    private Guid pendingNonce;
    private TaskCompletionSource? pendingPong;
    private static readonly TimeSpan HeartbeatInterval = TimeSpan.FromSeconds(5);
    public Peer(TcpClient client) : this(client.GetStream(), client.Dispose) { }
    internal Peer(Stream stream, Action close) { this.stream = stream; this.close = close; }
    internal static byte[] Encode(Message message)
    {
        var bytes = JsonSerializer.SerializeToUtf8Bytes(message);
        if (bytes.Length > MaxFrame) throw new InvalidDataException("Frame too large.");
        return bytes;
    }
    public async Task SendAsync(Message message, CancellationToken token) => await SendEncodedAsync(Encode(message), token).ConfigureAwait(false);
    internal async Task SendEncodedAsync(byte[] bytes, CancellationToken token)
    {
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(token);
        deadline.CancelAfter(Timeout);
        await sendLock.WaitAsync(deadline.Token).ConfigureAwait(false);
        try
        {
            deadline.Token.ThrowIfCancellationRequested();
            ObjectDisposedException.ThrowIf(Volatile.Read(ref disposed) != 0, this);
            var header = new byte[4];
            BinaryPrimitives.WriteInt32BigEndian(header, bytes.Length);
            try
            {
                await stream.WriteAsync(header, deadline.Token).ConfigureAwait(false);
                await stream.WriteAsync(bytes, deadline.Token).ConfigureAwait(false);
            }
            catch { Dispose(); throw; }
        }
        finally { sendLock.Release(); }
    }
    public async Task<Message> ReadAsync(CancellationToken token)
    {
        while (true)
        {
            using var deadline = CancellationTokenSource.CreateLinkedTokenSource(token);
            deadline.CancelAfter(Timeout);
            var header = new byte[4];
            await stream.ReadExactlyAsync(header, deadline.Token).ConfigureAwait(false);
            int length = BinaryPrimitives.ReadInt32BigEndian(header);
            if (length is <= 0 or > MaxFrame) throw new InvalidDataException("Invalid frame length.");
            var data = new byte[length];
            await stream.ReadExactlyAsync(data, deadline.Token).ConfigureAwait(false);
            Message message;
            try { message = JsonSerializer.Deserialize<Message>(data) ?? throw new InvalidDataException("Empty frame."); }
            catch (JsonException ex) { throw new InvalidDataException("Invalid JSON.", ex); }
            if (message.Type is not ("ping" or "pong")) return message;
            if (heartbeatStop is null) throw new InvalidDataException("Heartbeat before handshake.");
            ValidateHeartbeat(data, message);
            if (message.Type == "ping")
                await SendHeartbeatAsync("pong", message.Nonce, token).ConfigureAwait(false);
            else
            {
                lock (heartbeatGate)
                {
                    if (pendingPong is null || pendingNonce != message.Nonce || !pendingPong.TrySetResult())
                        throw new InvalidDataException("Unsolicited or mismatched pong.");
                    pendingNonce = Guid.Empty;
                    pendingPong = null;
                }
            }
        }
    }
    private static void ValidateHeartbeat(byte[] data, Message message)
    {
        using var json = JsonDocument.Parse(data);
        var properties = json.RootElement.EnumerateObject().ToArray();
        if (properties.Length != 2 || properties.Count(p => p.NameEquals("Type")) != 1 ||
            properties.Count(p => p.NameEquals("Nonce")) != 1 || message.Nonce == Guid.Empty)
            throw new InvalidDataException("Invalid heartbeat.");
    }
    private Task SendHeartbeatAsync(string type, Guid nonce, CancellationToken token) =>
        SendEncodedAsync(JsonSerializer.SerializeToUtf8Bytes(new { Type = type, Nonce = nonce }), token);
    internal void StartKeepalive()
    {
        heartbeatStop = new CancellationTokenSource();
        heartbeatTask = Task.Run(() => KeepaliveAsync(heartbeatStop.Token));
    }
    private async Task KeepaliveAsync(CancellationToken token)
    {
        try
        {
            while (true)
            {
                await Task.Delay(HeartbeatInterval, token).ConfigureAwait(false);
                var nonce = Guid.NewGuid();
                var pong = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                lock (heartbeatGate) { pendingNonce = nonce; pendingPong = pong; }
                using var deadline = CancellationTokenSource.CreateLinkedTokenSource(token);
                deadline.CancelAfter(Timeout);
                await SendHeartbeatAsync("ping", nonce, deadline.Token).ConfigureAwait(false);
                await pong.Task.WaitAsync(deadline.Token).ConfigureAwait(false);
            }
        }
        catch (Exception ex) when (ex is OperationCanceledException or IOException or SocketException or ObjectDisposedException)
        {
            if (!token.IsCancellationRequested) Dispose();
        }
    }
    internal async Task StopKeepaliveAsync()
    {
        await heartbeatTask.ConfigureAwait(false);
        heartbeatStop?.Dispose();
    }
    public void Dispose()
    {
        if (Interlocked.Exchange(ref disposed, 1) == 0) { heartbeatStop?.Cancel(); close(); }
    }
}
