using System.Text.Json;
using System.Text.Json.Serialization;
using Wsm.Core;

namespace Wsm.InGame.Runtime;

[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
internal sealed record GameplayRequest(
    [property: JsonRequired] string Kind,
    [property: JsonRequired] long RequestId,
    float? X = null, float? Y = null, int? Level = null, string? Choice = null,
    WorldSnapshot? Snapshot = null, RewardReceipt? Reward = null, ObservedBotFrame? Bot = null, SoloPartyFrame? Party = null);

internal sealed record GameplayEvent(int Epoch, long EventId, string Kind, Guid PlayerId,
    long RequestId = 0, long AgeMilliseconds = 0, PlayerInput? Input = null,
    UpgradeChoice? Upgrade = null, WorldSnapshot? Snapshot = null, RewardReceipt? Reward = null,
    string? Error = null, ObservedBotDecision? BotDecision = null, SoloPartyDecision? PartyDecision = null);
internal sealed record GameplayInfo(int Epoch, string Role, Guid PlayerId, bool Active, string? Error);

[JsonSourceGenerationOptions(PropertyNamingPolicy = JsonKnownNamingPolicy.CamelCase,
    DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull, GenerationMode = JsonSourceGenerationMode.Metadata)]
[JsonSerializable(typeof(GameplayRequest))]
[JsonSerializable(typeof(GameplayEvent))]
[JsonSerializable(typeof(GameplayInfo))]
internal partial class GameplayJsonContext : JsonSerializerContext;

// Each instance belongs to exactly one session epoch. Movement and snapshots
// coalesce; upgrades and rewards do not. The reward cap matches Core's accepted
// receipt ledger, so this adapter cannot silently drop an accepted Core reward.
internal sealed class GameplayMailbox(int epoch)
{
    internal const int MaxRequestBytes = 262144, MaxEventBytes = 524288;
    private const int MaxControls = 512, MaxRewards = 100000, MaxInputOwners = 16, MaxKnownPlayers = 1024;
    private readonly object gate = new();
    private readonly Dictionary<Guid, (GameplayEvent Frame, long Received)> inputs = [];
    private readonly HashSet<Guid> departed = [];
    private readonly HashSet<Guid> known = [];
    private readonly Dictionary<Guid, Queue<(string Kind, PlayerInput? Input, UpgradeChoice? Upgrade)>> awaitingJoin = [];
    private int awaitingCount, queuedNonLifecycleControls;
    private readonly Dictionary<Guid, int> deliveredRewards = [];
    private readonly Queue<GameplayEvent> controls = new(), rewards = new(), submissions = new();
    private readonly HashSet<long> pendingRequests = [];
    private GameplayEvent? snapshot, terminal;
    private long nextId;
    private bool cleared, ended, producersStopped;
    internal int Epoch => epoch;
    internal int RewardCount { get { lock (gate) return rewards.Count; } }
    /// <summary>Distinct receipts this mailbox has ever accepted, delivered or still queued.</summary>
    internal int DeliveredRewardCount { get { lock (gate) return deliveredRewards.Count; } }
    private string? lastError;
    internal string? Error { get { lock (gate) return lastError; } }
    internal bool HasReliable { get { lock (gate) return controls.Count != 0 || rewards.Count != 0 || pendingRequests.Count != 0 ||
        awaitingJoin.Values.Any(held => held.Any(item => item.Kind == "upgrade")); } }
    // An ID is unique until its result is consumed; every accepted request owns a result slot.
    internal int Reserve(long requestId)
    {
        lock (gate)
        {
            if (pendingRequests.Contains(requestId)) return NativeExports.InvalidArgument;
            if (cleared || ended) return NativeExports.Unavailable;
            if (pendingRequests.Count >= MaxControls) return NativeExports.QueueFull;
            pendingRequests.Add(requestId);
            return NativeExports.Accepted;
        }
    }
    internal void Unreserve(long requestId) { lock (gate) pendingRequests.Remove(requestId); }
    internal void Add(string kind, Guid player, PlayerInput? input = null, UpgradeChoice? upgrade = null,
        WorldSnapshot? world = null, RewardReceipt? reward = null, long requestId = 0, string? error = null,
        ObservedBotDecision? botDecision = null, SoloPartyDecision? partyDecision = null)
    {
        lock (gate)
        {
            if (cleared) return;
            if (departed.Contains(player) && kind is "input" or "joined" or "left") return;
            if (kind == "joined" && known.Contains(player)) return;
            if (ended && kind is "input" or "snapshot") return;
            if (producersStopped && kind == "joined") return;
            // Host lifecycle, commands and disconnects arrive on independent readers, so a
            // command or a leave can be observed before its own join. Causality per player is
            // join -> commands -> leave, so hold that player's later events until its join is
            // reported. Reliable events are never dropped here, only reordered.
            if (kind is "input" or "upgrade" or "left" && player != Guid.Empty &&
                !known.Contains(player) && !departed.Contains(player))
            {
                if (awaitingCount >= MaxControls) throw new InvalidOperationException("Gameplay pre-join queue capacity reached.");
                if (!awaitingJoin.TryGetValue(player, out var pending))
                {
                    if (awaitingJoin.Count >= MaxInputOwners) throw new InvalidOperationException("Gameplay pre-join identity capacity reached.");
                    pending = new Queue<(string, PlayerInput?, UpgradeChoice?)>();
                    awaitingJoin[player] = pending;
                }
                pending.Enqueue((kind, input, upgrade));
                awaitingCount++;
                return;
            }
            var frame = new GameplayEvent(epoch, checked(++nextId), kind, player, RequestId: requestId,
                Input: input, Upgrade: upgrade, Snapshot: world, Reward: reward, Error: error, BotDecision: botDecision, PartyDecision: partyDecision);
            if (input is not null)
            {
                if (!inputs.ContainsKey(player) && inputs.Count >= MaxInputOwners) throw new InvalidOperationException("Gameplay input owner capacity reached.");
                inputs[player] = (frame, Environment.TickCount64);
            }
            else if (world is not null) snapshot = frame;
            else if (kind == "submission") { submissions.Enqueue(frame); if (error is not null) lastError = error; }
            else if (kind == "joined")
            {
                if (!known.Contains(player))
                {
                    if (known.Count >= MaxKnownPlayers) throw new InvalidOperationException("Gameplay player identity capacity reached.");
                    known.Add(player);
                }
                // Lifecycle events are bounded by the known/departed identity limits, not
                // the much smaller choice queue. Keep every join and leave in order.
                controls.Enqueue(frame);
                if (awaitingJoin.Remove(player, out var held))
                    while (held!.Count != 0)
                    {
                        var pendingEvent = held.Dequeue();
                        awaitingCount--;
                        Add(pendingEvent.Item1, player, pendingEvent.Item2, pendingEvent.Item3);
                    }
            }
            else if (reward is not null)
            {
                var receiptId = reward.ReceiptId;
                if (deliveredRewards.TryGetValue(receiptId, out _)) return;
                if (rewards.Count >= MaxRewards) throw new InvalidOperationException("Gameplay reward inbox capacity reached.");
                if (deliveredRewards.Count >= MaxRewards) throw new InvalidOperationException("Gameplay reward history capacity reached.");
                deliveredRewards[receiptId] = 1;
                rewards.Enqueue(frame);
            }
            else
            {
                if (kind != "left" && queuedNonLifecycleControls >= MaxControls) throw new InvalidOperationException("Gameplay control inbox capacity reached.");
                if (kind != "left") queuedNonLifecycleControls++;
                controls.Enqueue(frame);
                // A leave is only reported after its join, so the tombstone that rejects later
                // commands for this identity is applied here rather than by the caller.
                if (kind == "left") Tombstone(player);
            }
        }
    }
    private void Tombstone(Guid player)
    {
        if (!departed.Contains(player) && departed.Count >= 100000) throw new InvalidOperationException("Gameplay departed identity capacity reached.");
        departed.Add(player);
        inputs.Remove(player);
        known.Remove(player);
        if (awaitingJoin.Remove(player, out var held)) awaitingCount -= held!.Count;
    }
    internal void RemovePlayer(Guid player)
    {
        lock (gate) Tombstone(player);
    }
    internal void End(string error, bool final = true)
    {
        lock (gate)
        {
            if (cleared) return;
            inputs.Clear(); snapshot = null;
            ended = true;
            lastError = error;
            // A pump error stops input immediately, but only its owner can publish
            // the terminal event after all sibling readers and outgoing work stop.
            if (final)
            {
                producersStopped = true;
                // The owner first reconciles real Core joins after stopping producers.
                // If provenance is unavailable, retain the decision as an explicit
                // delivery failure, never fabricate a normal joined/upgrade sequence.
                // The spill is bounded by the existing 512 pre-join event slots.
                foreach (var (player, held) in awaitingJoin)
                    foreach (var item in held)
                        if (item.Upgrade is { } upgrade)
                        {
                            controls.Enqueue(new GameplayEvent(epoch, checked(++nextId), "undelivered", player,
                                Upgrade: upgrade, Error: "join_unconfirmed_at_session_end"));
                            queuedNonLifecycleControls++;
                        }
                awaitingJoin.Clear(); awaitingCount = 0;
                terminal = new GameplayEvent(epoch, checked(++nextId), "ended", Guid.Empty, Error: error);
            }
        }
    }
    internal void Clear()
    {
        lock (gate)
        {
            cleared = true; inputs.Clear(); controls.Clear(); rewards.Clear(); submissions.Clear(); pendingRequests.Clear();
            departed.Clear(); deliveredRewards.Clear(); known.Clear(); awaitingJoin.Clear(); awaitingCount = 0;
            snapshot = terminal = null;
        }
    }
    internal int Poll(Span<byte> destination)
    {
        lock (gate)
        {
            GameplayEvent? frame = null;
            var source = 0;
            Guid inputOwner = default;
            void Consider(GameplayEvent? candidate, int candidateSource)
            {
                if (candidate is not null && (frame is null || candidate.EventId < frame.EventId))
                { frame = candidate; source = candidateSource; }
            }
            foreach (var item in inputs)
            {
                var candidate = item.Value.Frame with { AgeMilliseconds = Math.Max(0, Environment.TickCount64 - item.Value.Received) };
                if (frame is null || candidate.EventId < frame.EventId)
                { frame = candidate; source = 1; inputOwner = item.Key; }
            }
            if (controls.TryPeek(out var control)) Consider(control, 2);
            if (rewards.TryPeek(out var reward)) Consider(reward, 3);
            Consider(snapshot, 4);
            if (submissions.TryPeek(out var submission)) Consider(submission, 5);
            if (pendingRequests.Count == 0 && controls.Count == 0 && rewards.Count == 0 && submissions.Count == 0 && awaitingCount == 0) Consider(terminal, 6);
            if (frame is null) return 0;
            var bytes = JsonSerializer.SerializeToUtf8Bytes(frame, GameplayJsonContext.Default.GameplayEvent);
            if (bytes.Length > MaxEventBytes) return NativeExports.Unavailable;
            if (destination.IsEmpty) return bytes.Length;
            if (destination.Length <= bytes.Length) return NativeExports.InsufficientCapacity;
            bytes.CopyTo(destination); destination[bytes.Length] = 0;
            switch (source)
            {
                case 1: inputs.Remove(inputOwner); break;
                case 2:
                    var removed = controls.Dequeue();
                    if (removed.Kind is not ("joined" or "left")) queuedNonLifecycleControls--;
                    break;
                case 3: rewards.Dequeue(); break;
                case 4: snapshot = null; break;
                case 5: pendingRequests.Remove(submissions.Dequeue().RequestId); break;
                case 6: terminal = null; break;
            }
            return bytes.Length;
        }
    }
    internal static GameplayRequest? Parse(ReadOnlySpan<byte> bytes)
    {
        if (bytes.Length is < 1 or > MaxRequestBytes) return null;
        try
        {
            using var document = JsonDocument.Parse(bytes.ToArray(), new JsonDocumentOptions { MaxDepth = 16 });
            if (!UniqueProperties(document.RootElement)) return null;
            return JsonSerializer.Deserialize(bytes, GameplayJsonContext.Default.GameplayRequest);
        }
        catch (JsonException) { return null; }
    }
    private static bool UniqueProperties(JsonElement element)
    {
        if (element.ValueKind == JsonValueKind.Object)
        {
            var names = new HashSet<string>(StringComparer.Ordinal);
            foreach (var p in element.EnumerateObject())
                if (!names.Add(p.Name) || !UniqueProperties(p.Value)) return false;
        }
        else if (element.ValueKind == JsonValueKind.Array)
            foreach (var item in element.EnumerateArray()) if (!UniqueProperties(item)) return false;
        return true;
    }
}
