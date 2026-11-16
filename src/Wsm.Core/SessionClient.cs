using System.Net;
using System.Net.Sockets;
using System.Threading.Channels;

namespace Wsm.Core;

public sealed class SessionClient : IAsyncDisposable
{
    private readonly Peer peer;
    private readonly CancellationTokenSource stop = new();
    private readonly Channel<WorldSnapshot> snapshots = Channel.CreateBounded<WorldSnapshot>(new BoundedChannelOptions(16) { FullMode = BoundedChannelFullMode.DropOldest, SingleWriter = true });
    private Task receiveTask = Task.CompletedTask;
    private readonly object disposeLock = new();
    private Task? disposeTask;
    public Guid PlayerId { get; }
    public SessionSettings Settings { get; }
    public LocalProgression Progression { get; }
    private readonly Channel<RewardReceipt> rewards = Channel.CreateBounded<RewardReceipt>(new BoundedChannelOptions(128) { FullMode = BoundedChannelFullMode.Wait, SingleWriter = true });
    private readonly Dictionary<Guid, RewardReceipt> rewardStore = [];
    private readonly object rewardLock = new();
    public ChannelReader<RewardReceipt> Rewards => rewards.Reader;
    public RewardReceipt[] ReceivedRewards { get { lock (rewardLock) return rewardStore.Values.ToArray(); } }
    public int ReceivedRewardCount { get { lock (rewardLock) return rewardStore.Count; } }
    public ChannelReader<WorldSnapshot> Snapshots => snapshots.Reader;
    private SessionClient(Peer peer, Guid playerId, SessionSettings settings)
    { this.peer = peer; PlayerId = playerId; Settings = settings; Progression = new(playerId); }
    public static async Task<SessionClient> ConnectAsync(IPAddress address, int port, string password, CancellationToken token = default)
    {
        ArgumentNullException.ThrowIfNull(address);
        if (password is null || password.Length > 256) throw new ArgumentException("Invalid password.");
        var tcp = new TcpClient(address.AddressFamily);
        try
        {
            using var deadline = CancellationTokenSource.CreateLinkedTokenSource(token);
            deadline.CancelAfter(TimeSpan.FromSeconds(30));
            await tcp.ConnectAsync(address, port, deadline.Token);
            var peer = new Peer(tcp);
            await peer.SendAsync(new Message("join", Version: SessionHost.ProtocolVersion, Password: password), deadline.Token);
            var response = await peer.ReadAsync(deadline.Token);
            if (response.Type != "welcome" || response.Version != SessionHost.ProtocolVersion || response.PlayerId == Guid.Empty || response.Settings is null)
                throw new InvalidDataException(response.Error ?? "Invalid welcome.");
            response.Settings.Validate();
            var client = new SessionClient(peer, response.PlayerId, response.Settings);
            peer.StartKeepalive();
            client.receiveTask = Task.Run(client.ReceiveAsync);
            return client;
        }
        catch { tcp.Dispose(); throw; }
    }
    public Task SendInputAsync(PlayerInput input, CancellationToken token = default)
    {
        ArgumentNullException.ThrowIfNull(input);
        if (input.PlayerId != PlayerId || !input.Valid) throw new ArgumentException("Invalid input owner or movement.");
        return peer.SendAsync(new Message("input", Input: input), token);
    }
    public Task SendUpgradeAsync(UpgradeChoice choice, CancellationToken token = default)
    {
        ArgumentNullException.ThrowIfNull(choice);
        if (choice.PlayerId != PlayerId || choice.Level is <= 0 or > 10000 || string.IsNullOrWhiteSpace(choice.Choice) || choice.Choice.Length > 128) throw new ArgumentException("Invalid upgrade.");
        return peer.SendAsync(new Message("upgrade", Upgrade: choice), token);
    }
    private async Task ReceiveAsync()
    {
        Exception? error = null;
        try
        {
            while (!stop.IsCancellationRequested)
            {
                var message = await peer.ReadAsync(stop.Token);
                if (message.Type == "snapshot" && message.Snapshot is { Valid: true } snapshot) snapshots.Writer.TryWrite(snapshot);
                else if (message.Type == "reward" && message.Reward is { Valid: true } reward && reward.RecipientId == PlayerId)
                {
                    lock (rewardLock)
                    {
                        if (rewardStore.TryGetValue(reward.ReceiptId, out var prior))
                        {
                            if (prior != reward) throw new InvalidDataException("Conflicting reward receipt.");
                            continue;
                        }
                        if (rewardStore.Count >= 100_000) throw new InvalidDataException("Reward receipt capacity reached.");
                        if (reward.Kind == RewardKind.Xp && !Progression.ApplyXp(reward.ReceiptId, PlayerId, reward.Amount)) throw new InvalidDataException("XP receipt already used outside reward channel.");
                        rewardStore.Add(reward.ReceiptId, reward);
                        // The ledger records this receipt before notification. A full channel
                        // still terminates the receive loop; the adapter reconciles the ledger
                        // on teardown, but cannot promise continued session liveness.
                        if (!rewards.Writer.TryWrite(reward)) throw new InvalidDataException("Reward channel full.");
                    }
                }
                else throw new InvalidDataException("Invalid host message.");
            }
        }
        catch (Exception ex) { if (!stop.IsCancellationRequested || ex is not (OperationCanceledException or ObjectDisposedException or IOException or SocketException)) error = ex; }
        finally { peer.Dispose(); await peer.StopKeepaliveAsync(); snapshots.Writer.TryComplete(error); rewards.Writer.TryComplete(error); }
    }
    public ValueTask DisposeAsync()
    {
        lock (disposeLock) return new ValueTask(disposeTask ??= DisposeCoreAsync());
    }
    private async Task DisposeCoreAsync()
    {
        try { stop.Cancel(); peer.Dispose(); await receiveTask; }
        finally { stop.Dispose(); }
    }
}
