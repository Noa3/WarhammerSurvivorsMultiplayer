using System.Text.Json;
using System.Threading.Channels;
using Wsm.Core;

namespace Wsm.InGame.Runtime;

public sealed partial class IngameSessionController
{
    private GameplayMailbox? retainedGameplay;
    public int GameplayEpoch { get { lock (gate) return (active?.Gameplay ?? retainedGameplay)?.Epoch ?? 0; } }
    public Guid GameplayPlayerId { get { lock (gate) return active?.Client?.PlayerId ?? Guid.Empty; } }
    internal int PendingGameplayRewards { get { lock (gate) return (active?.Gameplay ?? retainedGameplay)?.RewardCount ?? 0; } }

    /// <summary>Returns required UTF-8 bytes, or copies and consumes one event including NUL.
    /// An empty span queries length; too-small buffers never consume an event. Inputs and
    /// world snapshots coalesce, reliable decisions/rewards remain until consumed or Leave.</summary>
    public int PollGameplay(Span<byte> destination)
    {
        lock (gate)
        {
            var mailbox = active?.Gameplay ?? retainedGameplay;
            if (mailbox is null) return 0;
            // Core's ledger is authoritative and its notification channel is bounded. Anything
            // the channel could not carry is reconciled here, so a stalled consumer never loses
            // an accepted receipt while the session is still live.
            if (active is { Client: { } client }) ReconcileCoreRewards(session: active, client);
            return mailbox.Poll(destination);
        }
    }

    // Idempotent: the mailbox deduplicates by receipt id, so replaying the ledger is safe.
    private void ReconcileCoreRewards(Session session, SessionClient client)
    {
        var mailbox = session.Gameplay;
        lock (gate)
        {
            if (client.ReceivedRewardCount <= mailbox.DeliveredRewardCount) return;
            foreach (var receipt in client.ReceivedRewards)
                mailbox.Add("reward", client.PlayerId, reward: receipt);
        }
    }

    internal byte[] GameplayInfoPayload()
    {
        lock (gate)
        {
            var mailbox = active?.Gameplay ?? retainedGameplay;
            var role = active?.Host is not null ? "host" : active?.Client is not null ? "guest" : "none";
            return JsonSerializer.SerializeToUtf8Bytes(new GameplayInfo(mailbox?.Epoch ?? 0, role,
                active?.Client?.PlayerId ?? Guid.Empty, active is { Abandoned: false } &&
                current.State is IngameState.Hosting or IngameState.Connected, mailbox?.Error), GameplayJsonContext.Default.GameplayInfo);
        }
    }

    /// <summary>Validates and queues a request for exactly the specified current session.
    /// Accepted means queued, not applied to the game or acknowledged by the remote peer.
    /// The native input owner is assigned here, never trusted from caller JSON.</summary>
    public int SubmitGameplay(int expectedEpoch, ReadOnlySpan<byte> utf8)
    {
        var request = GameplayMailbox.Parse(utf8);
        if (request is null || request.RequestId <= 0) return NativeExports.InvalidArgument;
        lock (gate)
        {
            var session = active;
            if (disposed != 0 || session is null || session.Abandoned || session.Cancellation.IsCancellationRequested || session.Generation != expectedEpoch ||
                current.State is not (IngameState.Hosting or IngameState.Connected)) return NativeExports.Unavailable;
            if (!ValidSubmission(request, session)) return NativeExports.InvalidArgument;
            var reservation = session.Gameplay.Reserve(request.RequestId);
            if (reservation != NativeExports.Accepted) return reservation;
            if (session.GameplayOutgoing.Writer.TryWrite(request)) return NativeExports.Accepted;
            session.Gameplay.Unreserve(request.RequestId);
            return NativeExports.QueueFull;
        }
    }
    private static bool ValidSubmission(GameplayRequest r, Session session)
    {
        var plain = r.Snapshot is null && r.Reward is null;
        if (r.Party is { } party)
            return r.Kind == "soloParty" && session.Host is { ConnectedClients: 0 } solo && plain && r.Bot is null &&
                r.X is null && r.Y is null && r.Level is null && r.Choice is null && party.Valid(solo.Settings.BotCount);
        if (r.Bot is not null)
            return r.Kind == "botFrame" && session.Host is { } owner && plain && r.X is null && r.Y is null &&
                r.Level is null && r.Choice is null && r.Bot.Valid(owner.Settings.BotCount);
        if (session.Client is { } client)
            return r.Kind switch
            {
                "input" => plain && r.X.HasValue && r.Y.HasValue && r.Level is null && r.Choice is null &&
                    SessionPayloadValidation.IsValid(new PlayerInput(client.PlayerId, r.X.Value, r.Y.Value)),
                "upgrade" => plain && r.X is null && r.Y is null && r.Level.HasValue &&
                    SessionPayloadValidation.IsValid(new UpgradeChoice(client.PlayerId, r.Level.Value, r.Choice!)),
                _ => false,
            };
        if (session.Host is not null && r.X is null && r.Y is null && r.Level is null && r.Choice is null)
            return r.Kind switch
            {
                "snapshot" => r.Reward is null && SessionPayloadValidation.IsValid(r.Snapshot) &&
                    r.Snapshot!.Enemies is { Length: 0 } &&
                    r.Snapshot.Entities.Select(e => e.Id).Distinct(StringComparer.Ordinal).Count() == r.Snapshot.Entities.Length,
                "reward" => r.Snapshot is null && SessionPayloadValidation.IsValid(r.Reward),
                _ => false,
            };
        return false;
    }
    private async Task GameplayOutgoingAsync(Session session)
    {
        try
        {
            await foreach (var request in session.GameplayOutgoing.Reader.ReadAllAsync(session.Cancellation.Token).ConfigureAwait(false))
            {
                if (session.Abandoned) return;
                string? error = null;
                ObservedBotDecision? botDecision = null;
                SoloPartyDecision? partyDecision = null;
                try
                {
                    if (session.Client is { } client)
                    {
                        if (request.Kind == "input") await client.SendInputAsync(new PlayerInput(client.PlayerId, request.X!.Value, request.Y!.Value), session.Cancellation.Token).ConfigureAwait(false);
                        else await client.SendUpgradeAsync(new UpgradeChoice(client.PlayerId, request.Level!.Value, request.Choice!), session.Cancellation.Token).ConfigureAwait(false);
                    }
                    else if (session.Host is { } host)
                    {
                        if (request.Kind == "snapshot") await host.PublishSnapshotAsync(request.Snapshot!, session.Cancellation.Token).ConfigureAwait(false);
                        else if (request.Party is { } party)
                        {
                            if (host.ConnectedClients != 0) throw new InvalidOperationException("Solo party requires no guests.");
                            partyDecision = session.BotRoster.PlanParty(party, host.Settings.BotDifficulty);
                        }
                        else if (request.Bot is { } frame) botDecision = session.BotRoster.Plan(frame, host.Settings);
                        else host.PublishReward(request.Reward!.RecipientId, request.Reward);
                    }
                }
                catch (OperationCanceledException) when (session.Cancellation.IsCancellationRequested)
                {
                    session.Gameplay.Add("submission", session.Client?.PlayerId ?? Guid.Empty, requestId: request.RequestId, error: "cancelled");
                    return;
                }
                catch (Exception ex) { error = ex.GetType().Name; }
                session.Gameplay.Add("submission", session.Client?.PlayerId ?? Guid.Empty, requestId: request.RequestId, error: error,
                    botDecision: botDecision, partyDecision: partyDecision);
            }
        }
        catch (OperationCanceledException) when (session.Cancellation.IsCancellationRequested) { }
        finally { DrainGameplayOutgoing(session); }
    }
    private async Task ReadGameplayAsync<T>(ChannelReader<T> reader, CancellationToken token, Action<T> accept, string channel)
    {
        // Pause before reading, so a test can hold a whole channel and exercise a stalled
        // consumer instead of only a slow accept.
        while (true)
        {
            if (StallGameplayChannelForTests is { } stall)
            {
                var pause = stall(channel);
                if (pause > TimeSpan.Zero) await Task.Delay(pause, token).ConfigureAwait(false);
            }
            if (!await reader.WaitToReadAsync(token).ConfigureAwait(false)) break;
            while (reader.TryRead(out var item)) accept(item);
        }
    }
    private void DrainGameplayOutgoing(Session session)
    {
        lock (gate)
        {
            session.GameplayOutgoing.Writer.TryComplete();
            while (session.GameplayOutgoing.Reader.TryRead(out var pending))
                session.Gameplay.Add("submission", session.Client?.PlayerId ?? Guid.Empty, requestId: pending.RequestId, error: "cancelled");
        }
    }
    private async Task HostGameplayPumpAsync(Session session, SessionHost host)
    {
        async Task Guard(Func<Task> run)
        {
            try { await run().ConfigureAwait(false); }
            catch (OperationCanceledException) when (session.Cancellation.IsCancellationRequested) { }
            catch (Exception ex)
            {
                session.Gameplay.End("gameplay_backpressure_or_transport_" + ex.GetType().Name, final: false);
                session.Cancellation.Cancel();
            }
        }
        if (session.Cancellation.IsCancellationRequested) { DrainGameplayOutgoing(session); return; }
        var token = session.Cancellation.Token;
        await Task.WhenAll(
            Guard(() => ReadGameplayAsync(host.Commands, token, c => session.Gameplay.Add(c.Input is not null ? "input" : "upgrade", c.PlayerId, input: c.Input, upgrade: c.Upgrade), "commands")),
            Guard(() => ReadGameplayAsync(host.Joined, token, id => { session.Gameplay.Add("joined", id); PublishHosting(session, host); }, "joined")),
            Guard(() => ReadGameplayAsync(host.Disconnected, token, id => { session.Gameplay.Add("left", id); PublishHosting(session, host); }, "disconnected")),
            Guard(() => GameplayOutgoingAsync(session))).ConfigureAwait(false);
        // Stop Core before reconciling its authoritative lifecycle channel, so
        // no new admission can race the final drain. Core disposal is memoized
        // and does not await this adapter pump.
        try { await host.DisposeAsync().ConfigureAwait(false); }
        catch (Exception ex) { session.Gameplay.End("shutdown_" + ex.GetType().Name, final: false); }
        if (!session.Abandoned)
            while (host.Joined.TryRead(out var admitted))
            {
                try { session.Gameplay.Add("joined", admitted); }
                catch (InvalidOperationException ex)
                {
                    session.Gameplay.End("join_reconciliation_" + ex.GetType().Name, final: false);
                }
            }
        FinishGameplayLoss(session, IngameFailure.Internal);
    }
    private async Task GuestGameplayPumpAsync(Session session, SessionClient client)
    {
        async Task Receive(Func<Task> run)
        {
            try { await run().ConfigureAwait(false); }
            catch (OperationCanceledException) when (session.Cancellation.IsCancellationRequested) { }
            catch (Exception ex) { session.Gameplay.End("transport_" + ex.GetType().Name, final: false); }
        }
        if (session.Cancellation.IsCancellationRequested) { DrainGameplayOutgoing(session); return; }
        var token = session.Cancellation.Token;
        var outgoing = GameplayOutgoingAsync(session);
        await Task.WhenAll(
            Receive(() => ReadGameplayAsync(client.Snapshots, token, s => session.Gameplay.Add("snapshot", client.PlayerId, world: s), "snapshots")),
            Receive(() => ReadGameplayAsync(client.Rewards, token, r => session.Gameplay.Add("reward", client.PlayerId, reward: r), "rewards"))).ConfigureAwait(false);
        // The pump may stop after Core accepted receipts into its durable ledger but before it
        // moved every one into the mailbox. Reconcile the ledger so none can be lost at teardown.
        ReconcileCoreRewards(session, client);
        session.Cancellation.Cancel();
        try { await outgoing.ConfigureAwait(false); } catch (Exception ex) { session.Gameplay.End("send_" + ex.GetType().Name, final: false); }
        FinishGameplayLoss(session, IngameFailure.HostLost);
    }
    private void FinishGameplayLoss(Session session, IngameFailure failure)
    {
        lock (gate)
        {
            if (!ReferenceEquals(active, session)) return;
            active = null; pendingRelease = session; session.Abandoned = true;
            retainedGameplay = session.Gameplay;
            session.Gameplay.End(failure == IngameFailure.HostLost ? "host_lost" : "gameplay_transport_failed");
            if (failure == IngameFailure.HostLost) PublishDisconnected(failure);
            else Publish(IngameState.Failed, failure, 0, 0, "", false);
        }
        // The release awaits this pump. Schedule cleanup rather than self-await.
        session.LossCleanup = Task.Run(async () =>
        {
            try
            {
                await ReleaseAsync(session).ConfigureAwait(false);
                lock (gate) if (ReferenceEquals(pendingRelease, session)) { pendingRelease = null; session.Cancellation.Dispose(); }
            }
            catch (Exception ex)
            {
                System.Diagnostics.Trace.TraceError("Gameplay transport cleanup failed: {0}", ex.GetType().Name);
                lock (gate) if (ReferenceEquals(pendingRelease, session)) Publish(IngameState.Failed, IngameFailure.Internal, 0, 0, "", false);
            }
        });
    }
}
