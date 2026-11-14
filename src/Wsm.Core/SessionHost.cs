using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Threading.Channels;

namespace Wsm.Core;

public sealed class SessionHost : IAsyncDisposable
{
    public const int ProtocolVersion = 2;
    public Encounter Encounter { get; } = new();
    private readonly SessionSettings settings;
    private readonly string password;
    private readonly TcpListener listener;
    private readonly CancellationTokenSource stop = new();
    private readonly ConcurrentDictionary<Guid, Outbound> peers = new();
    private readonly ConcurrentQueue<Exception> workerErrors = new();
    private readonly object disposeLock = new();
    private Task? disposeTask;
    private readonly ConcurrentDictionary<int, Task> workers = new();
    private readonly SemaphoreSlim slots;
    private readonly Channel<SessionCommand> commands = Channel.CreateBounded<SessionCommand>(128);
    private readonly Channel<Guid> disconnected = Channel.CreateBounded<Guid>(128);
    private readonly Channel<Guid> joined = Channel.CreateBounded<Guid>(128);
    private Task? acceptTask;
    private int workerId;
    public ChannelReader<SessionCommand> Commands => commands.Reader;
    public ChannelReader<Guid> Disconnected => disconnected.Reader;
    public ChannelReader<Guid> Joined => joined.Reader;
    public Guid[] ConnectedPlayerIds => peers.Keys.ToArray();
    internal (long Sent, bool Writing) OutboundStatus(Guid id) => peers[id].Status;
    public int ConnectedClients => peers.Count;
    public int Port => ((IPEndPoint)listener.LocalEndpoint).Port;
    public SessionSettings Settings => settings;
    public SessionHost(SessionSettings settings, string password, int port = 0, IPAddress? bindAddress = null)
    {
        ArgumentNullException.ThrowIfNull(settings);
        settings.Validate();
        if (password is null || password.Length > 256) throw new ArgumentException("Invalid password.");
        this.settings = settings;
        this.password = password;
        slots = new SemaphoreSlim(settings.MaxClients);
        listener = new TcpListener(bindAddress ?? IPAddress.Loopback, port);
    }
    public void Start()
    {
        if (acceptTask is not null) throw new InvalidOperationException("Already started.");
        listener.Start(settings.MaxClients);
        acceptTask = AcceptAsync();
    }
    private async Task AcceptAsync()
    {
        try
        {
            while (!stop.IsCancellationRequested)
            {
                var tcp = await listener.AcceptTcpClientAsync(stop.Token);
                if (!slots.Wait(0)) { tcp.Dispose(); continue; }
                var id = Interlocked.Increment(ref workerId);
                var task = HandleAsync(tcp).ContinueWith(t => { if (t.IsFaulted) workerErrors.Enqueue(t.Exception!); }, CancellationToken.None, TaskContinuationOptions.ExecuteSynchronously, TaskScheduler.Default);
                workers[id] = task;
                _ = task.ContinueWith(_ => workers.TryRemove(id, out var ignored), CancellationToken.None, TaskContinuationOptions.ExecuteSynchronously, TaskScheduler.Default);
            }
        }
        catch (OperationCanceledException) { }
        catch (ObjectDisposedException) when (stop.IsCancellationRequested) { }
        catch (SocketException) when (stop.IsCancellationRequested) { }
    }
    private async Task HandleAsync(TcpClient tcp)
    {
        using var peer = new Peer(tcp);
        Guid player = Guid.Empty;
        Outbound? outbound = null;
        try
        {
            var join = await peer.ReadAsync(stop.Token);
            if (join.Type != "join" || join.Version != ProtocolVersion || join.Password != password)
            {
                await peer.SendAsync(new Message("reject", Error: "Invalid join, version or password."), stop.Token);
                return;
            }
            player = Guid.NewGuid();
            await peer.SendAsync(new Message("welcome", Version: ProtocolVersion, PlayerId: player, Settings: settings), stop.Token);
            outbound = new Outbound(peer, stop.Token);
            peers[player] = outbound;
            joined.Writer.TryWrite(player);
            while (!stop.IsCancellationRequested)
            {
                var message = await peer.ReadAsync(stop.Token);
                SessionCommand command;
                if (message.Type == "input" && message.Input is { } input && input.PlayerId == player && input.Valid)
                    command = new(player, input, null);
                else if (message.Type == "upgrade" && message.Upgrade is { } choice && choice.PlayerId == player && choice.Level > 0 && choice.Level <= 10000 && !string.IsNullOrWhiteSpace(choice.Choice) && choice.Choice.Length <= 128)
                    command = new(player, null, choice);
                else throw new InvalidDataException("Unauthorized or invalid command.");
                if (!commands.Writer.TryWrite(command)) throw new InvalidDataException("Command queue full.");
            }
        }
        catch (Exception ex) when (ex is InvalidDataException or IOException or SocketException or OperationCanceledException or ObjectDisposedException) { }
        finally
        {
            if (player != Guid.Empty && peers.TryRemove(player, out _)) disconnected.Writer.TryWrite(player);
            peer.Dispose();
            try { if (outbound is not null) await outbound.StopAsync(); }
            finally { slots.Release(); }
        }
    }
    public async Task PublishSnapshotAsync(WorldSnapshot snapshot, CancellationToken token = default)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        if (snapshot.Enemies is not { Length: 0 }) throw new ArgumentException("Enemy health is host encounter state, not caller snapshot data.", nameof(snapshot));
        snapshot = snapshot with { Enemies = Encounter.Snapshot() };
        if (!snapshot.Valid) throw new ArgumentException("Invalid snapshot.", nameof(snapshot));
        token.ThrowIfCancellationRequested();
        var bytes = Peer.Encode(new Message("snapshot", Snapshot: snapshot));
        token.ThrowIfCancellationRequested();
        foreach (var peer in peers.Values) peer.Enqueue(bytes);
        await Task.CompletedTask;
    }
    public void PublishReward(Guid recipient, RewardReceipt reward)
    {
        ArgumentNullException.ThrowIfNull(reward);
        if (!reward.Valid || reward.RecipientId != recipient) throw new ArgumentException("Invalid reward or recipient.");
        if (!peers.TryGetValue(recipient, out var peer)) throw new InvalidOperationException("Recipient is not connected.");
        var bytes = Peer.Encode(new Message("reward", Reward: reward));
        if (!peer.EnqueueControl(bytes)) throw new InvalidOperationException("Recipient control queue full or disconnected; connection closed.");
    }
    private sealed class Outbound
    {
        private readonly Peer peer;
        private readonly object gate = new();
        private readonly Queue<byte[]> controls = new();
        private byte[]? snapshot;
        private bool closed;
        private readonly Channel<bool> wake = Channel.CreateBounded<bool>(new BoundedChannelOptions(1) { FullMode = BoundedChannelFullMode.DropWrite, SingleReader = true });
        private readonly Task worker;
        private long sent;
        private int writing;
        public (long Sent, bool Writing) Status => (Interlocked.Read(ref sent), Volatile.Read(ref writing) != 0);
        public Outbound(Peer peer, CancellationToken token) { this.peer = peer; worker = RunAsync(token); }
        public void Enqueue(byte[] bytes) { lock (gate) { if (closed) return; snapshot = bytes; wake.Writer.TryWrite(true); } }
        public bool EnqueueControl(byte[] bytes)
        {
            lock (gate)
            {
                if (closed) return false;
                if (controls.Count >= 128) { closed = true; peer.Dispose(); wake.Writer.TryComplete(); return false; }
                controls.Enqueue(bytes); wake.Writer.TryWrite(true); return true;
            }
        }
        private async Task RunAsync(CancellationToken token)
        {
            try
            {
                await foreach (var ignored in wake.Reader.ReadAllAsync(token))
                {
                    while (true)
                    {
                        byte[]? bytes;
                        lock (gate)
                        {
                            if (closed) return;
                            if (controls.Count > 0) bytes = controls.Dequeue();
                            else { bytes = snapshot; snapshot = null; }
                        }
                        if (bytes is null) break;
                        Volatile.Write(ref writing, 1);
                        try { await peer.SendEncodedAsync(bytes, token); Interlocked.Increment(ref sent); }
                        finally { Volatile.Write(ref writing, 0); }
                    }
                }
            }
            catch (Exception ex) when (ex is IOException or SocketException or OperationCanceledException or ObjectDisposedException) { }
            finally { lock (gate) { closed = true; wake.Writer.TryComplete(); } peer.Dispose(); }
        }
        public Task StopAsync() { lock (gate) { closed = true; wake.Writer.TryComplete(); } return worker; }
        public void Close() => peer.Dispose();
    }
    public ValueTask DisposeAsync()
    {
        lock (disposeLock) return new ValueTask(disposeTask ??= DisposeCoreAsync());
    }
    private async Task DisposeCoreAsync()
    {
        try
        {
            stop.Cancel(); listener.Stop();
            foreach (var peer in peers.Values) peer.Close();
            try { if (acceptTask is not null) await acceptTask; }
            catch (Exception ex) { workerErrors.Enqueue(ex); }
            await Task.WhenAll(workers.Values.ToArray());
        }
        finally
        {
            commands.Writer.TryComplete(); disconnected.Writer.TryComplete(); joined.Writer.TryComplete();
            stop.Dispose(); slots.Dispose();
        }
        if (!workerErrors.IsEmpty) throw new AggregateException("Session worker failed.", workerErrors);
    }
}
