using System.Buffers.Binary;
using System.Text.Json;
using System.Net.Sockets;

namespace Wsm.Core;

internal sealed record Message(string Type, int Version = 0, string? Password = null, Guid PlayerId = default, SessionSettings? Settings = null, PlayerInput? Input = null, UpgradeChoice? Upgrade = null, WorldSnapshot? Snapshot = null, string? Error = null, RewardReceipt? Reward = null);

internal sealed class Peer : IDisposable
{
    internal const int MaxFrame = 262144;
    private static readonly TimeSpan Timeout = TimeSpan.FromSeconds(30);
    private readonly SemaphoreSlim sendLock = new(1, 1);
    private readonly Stream stream;
    private readonly Action close;
    private int disposed;
    public Peer(TcpClient client) : this(client.GetStream(), client.Dispose) { }
    internal Peer(Stream stream, Action close) { this.stream = stream; this.close = close; }
    internal static byte[] Encode(Message message)
    {
        var bytes = JsonSerializer.SerializeToUtf8Bytes(message);
        if (bytes.Length > MaxFrame) throw new InvalidDataException("Frame too large.");
        return bytes;
    }
    public async Task SendAsync(Message message, CancellationToken token) => await SendEncodedAsync(Encode(message), token);
    internal async Task SendEncodedAsync(byte[] bytes, CancellationToken token)
    {
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(token);
        deadline.CancelAfter(Timeout);
        await sendLock.WaitAsync(deadline.Token);
        try
        {
            deadline.Token.ThrowIfCancellationRequested();
            ObjectDisposedException.ThrowIf(Volatile.Read(ref disposed) != 0, this);
            var header = new byte[4];
            BinaryPrimitives.WriteInt32BigEndian(header, bytes.Length);
            try
            {
                await stream.WriteAsync(header, deadline.Token);
                await stream.WriteAsync(bytes, deadline.Token);
            }
            catch { Dispose(); throw; }
        }
        finally { sendLock.Release(); }
    }
    public async Task<Message> ReadAsync(CancellationToken token)
    {
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(token);
        deadline.CancelAfter(Timeout);
        var header = new byte[4];
        await stream.ReadExactlyAsync(header, deadline.Token);
        int length = BinaryPrimitives.ReadInt32BigEndian(header);
        if (length is <= 0 or > MaxFrame) throw new InvalidDataException("Invalid frame length.");
        var data = new byte[length];
        await stream.ReadExactlyAsync(data, deadline.Token);
        try { return JsonSerializer.Deserialize<Message>(data) ?? throw new InvalidDataException("Empty frame."); }
        catch (JsonException ex) { throw new InvalidDataException("Invalid JSON.", ex); }
    }
    public void Dispose() { if (Interlocked.Exchange(ref disposed, 1) == 0) close(); }
}
