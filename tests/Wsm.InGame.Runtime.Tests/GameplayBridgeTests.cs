using System.Text;
using System.Text.Json;
using Wsm.Core;
using Wsm.InGame.Runtime;

internal static class GameplayBridgeTests
{
    private static object PrivateField(object obj, string name) => obj.GetType().GetField(name, System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!.GetValue(obj)!;
    private static object Session(IngameSessionController controller) => PrivateField(controller, "active");
    private static T SessionProperty<T>(object session, string name) => (T)session.GetType().GetProperty(name)!.GetValue(session)!;

    public static async Task PreJoinUpgradeSurvivesReaderFailure()
    {
        await using var host = new IngameSessionController();
        var failDisconnected = 0;
        host.StallGameplayChannelForTests = channel => channel switch
        {
            "joined" => TimeSpan.FromSeconds(30),
            "disconnected" when Volatile.Read(ref failDisconnected) != 0 => throw new InvalidOperationException("Independent reader failure"),
            _ => TimeSpan.Zero
        };
        Require(await host.HostAsync(0, false) == TransitionResult.Started, "host");
        var guest = await SessionClient.ConnectAsync(System.Net.IPAddress.Loopback, host.Status.BoundPort, "");
        var id = guest.PlayerId;
        var choice = new UpgradeChoice(id, 2, "retained-real-choice");
        try
        {
            await guest.SendUpgradeAsync(choice);
            var box = SessionProperty<GameplayMailbox>(Session(host), "Gameplay");
            var deadline = Environment.TickCount64 + 5000;
            while ((int)PrivateField(box, "awaitingCount") == 0 && Environment.TickCount64 < deadline) await Task.Delay(5);
            Require((int)PrivateField(box, "awaitingCount") != 0, "Core-accepted upgrade reached pre-join mailbox");
            var reliableBeforeFailure = box.HasReliable;
            Volatile.Write(ref failDisconnected, 1);
            await guest.DisposeAsync();
            deadline = Environment.TickCount64 + 10000;
            while (host.Status.State != IngameState.Failed && Environment.TickCount64 < deadline) await Task.Delay(5);
            Require(host.Status.State == IngameState.Failed, "independent reader failure ended host");
            var retry = await host.HostAsync(0, false);
            Require(reliableBeforeFailure && retry == TransitionResult.Busy, "pre-join reliable upgrade must prevent reconnect before delivery");
            using var joined = await ReadKind(host, "joined");
            Require(joined.RootElement.GetProperty("playerId").GetGuid() == id, "actual admitted join provenance");
            using var upgrade = await ReadKind(host, "upgrade");
            Require(upgrade.RootElement.GetProperty("playerId").GetGuid() == id &&
                upgrade.RootElement.GetProperty("upgrade").GetProperty("choice").GetString() == choice.Choice, "exact accepted upgrade retained after its join");
            using var left = await ReadKind(host, "left");
            using var ended = await ReadKind(host, "ended");
            deadline = Environment.TickCount64 + 10000;
            do { retry = await host.HostAsync(0, false); if (retry == TransitionResult.Busy) await Task.Delay(5); }
            while (retry == TransitionResult.Busy && Environment.TickCount64 < deadline);
            Require(retry == TransitionResult.Started, "drained retained upgrade permits retry");
        }
        finally { await guest.DisposeAsync(); }
    }

    public static Task PreJoinWithoutProvenanceRetainsFailure()
    {
        var box = new GameplayMailbox(1);
        var id = Guid.NewGuid();
        var choice = new UpgradeChoice(id, 3, "unconfirmed-choice");
        box.Add("upgrade", id, upgrade: choice);
        var reliableBeforeEnd = box.HasReliable;
        box.End("reader_failure", final: false);
        box.End("reader_failure");
        Require(reliableBeforeEnd && box.HasReliable, "pre-join reliable upgrade cannot disappear at terminal");
        var buffer = new byte[2048];
        var n = box.Poll(buffer);
        Require(n > 0, "undelivered decision event");
        using (var frame = JsonDocument.Parse(buffer.AsMemory(0, n)))
        {
            Require(frame.RootElement.GetProperty("kind").GetString() == "undelivered" &&
                frame.RootElement.GetProperty("playerId").GetGuid() == id &&
                frame.RootElement.GetProperty("upgrade").GetProperty("choice").GetString() == choice.Choice &&
                frame.RootElement.GetProperty("error").GetString() == "join_unconfirmed_at_session_end", "retain payload as explicit failure, never fabricate a join");
        }
        n = box.Poll(buffer);
        using var terminal = JsonDocument.Parse(buffer.AsMemory(0, n));
        Require(terminal.RootElement.GetProperty("kind").GetString() == "ended" && !box.HasReliable, "terminal follows retained failure");
        return Task.CompletedTask;
    }

    public static Task LifecycleBurst()
    {
        var box = new GameplayMailbox(1);
        var ids = new List<Guid>();
        for (int i = 0; i < 257; i++)
        {
            var id = Guid.NewGuid(); ids.Add(id);
            box.Add("joined", id);
            if (i == 100) box.Add("upgrade", id, upgrade: new UpgradeChoice(id, 2, "keep"));
            box.Add("left", id);
        }
        var buffer = new byte[GameplayMailbox.MaxEventBytes + 1];
        for (int i = 0; i < ids.Count; i++)
        {
            void Expect(string kind)
            {
                var n = box.Poll(buffer);
                Require(n > 0, "missing lifecycle or upgrade event");
                using var frame = JsonDocument.Parse(buffer.AsMemory(0, n));
                Require(frame.RootElement.GetProperty("kind").GetString() == kind && frame.RootElement.GetProperty("playerId").GetGuid() == ids[i], "event order and identity");
            }
            Expect("joined");
            if (i == 100) Expect("upgrade");
            Expect("left");
        }
        Require(box.Poll(buffer) == 0, "no extra events");
        return Task.CompletedTask;
    }

    public static async Task IdleRewardPollingAllocation()
    {
        await using var host = new IngameSessionController();
        await using var guest = new IngameSessionController();
        Require(await host.HostAsync(0, false) == TransitionResult.Started, "host");
        Require(await guest.JoinAsync("127.0.0.1", host.Status.BoundPort) == TransitionResult.Started, "guest");
        var id = Guid.NewGuid();
        var json = $"{{\"kind\":\"reward\",\"requestId\":1,\"reward\":{{\"receiptId\":\"{id}\",\"recipientId\":\"{guest.GameplayPlayerId}\",\"kind\":0,\"amount\":1}}}}";
        Require(host.SubmitGameplay(host.GameplayEpoch, Encoding.UTF8.GetBytes(json)) == 0, "reward submit");
        using var reward = await ReadKind(guest, "reward");
        for (int i = 0; i < 100; i++) guest.PollGameplay(Span<byte>.Empty);
        var before = GC.GetAllocatedBytesForCurrentThread();
        for (int i = 0; i < 1000; i++) Require(guest.PollGameplay(Span<byte>.Empty) == 0, "idle poll");
        var allocated = GC.GetAllocatedBytesForCurrentThread() - before;
        Require(allocated < 16000, $"1000 idle polls allocated {allocated} bytes after one receipt");
    }

    public static async Task CancelledInflight()
    {
        await using var host = new IngameSessionController();
        await using var guest = new IngameSessionController();
        Require(await host.HostAsync(0, false) == TransitionResult.Started, "host");
        Require(await guest.JoinAsync("127.0.0.1", host.Status.BoundPort) == TransitionResult.Started, "guest");
        var session = Session(guest);
        var client = SessionProperty<SessionClient>(session, "Client");
        var sendLock = (SemaphoreSlim)PrivateField(PrivateField(client, "peer"), "sendLock");
        await sendLock.WaitAsync();
        try
        {
            Require(guest.SubmitGameplay(guest.GameplayEpoch, Encoding.UTF8.GetBytes("{\"kind\":\"input\",\"requestId\":71,\"x\":0,\"y\":0}")) == 0, "submit");
            var outgoing = SessionProperty<System.Threading.Channels.Channel<GameplayRequest>>(session, "GameplayOutgoing");
            var deadline = Environment.TickCount64 + 5000;
            while (outgoing.Reader.Count != 0 && Environment.TickCount64 < deadline) await Task.Delay(5);
            Require(outgoing.Reader.Count == 0, "request must be in flight while send lock held");
            Require(guest.SubmitGameplay(guest.GameplayEpoch, Encoding.UTF8.GetBytes("{\"kind\":\"input\",\"requestId\":72,\"x\":0,\"y\":0}")) == 0, "queued second request");
            Require(outgoing.Reader.Count == 1, "second request remains queued");
            await host.LeaveAsync();
            deadline = Environment.TickCount64 + 10000;
            while (guest.Status.State != IngameState.Disconnected && Environment.TickCount64 < deadline) await Task.Delay(5);
            Require(guest.Status.State == IngameState.Disconnected, "host loss");
            using var submission = await ReadKind(guest, "submission");
            Require(submission.RootElement.GetProperty("requestId").GetInt64() == 71, "cancelled request correlation");
            Require(submission.RootElement.GetProperty("error").GetString() == "cancelled", "cancelled outcome");
            using var queued = await ReadKind(guest, "submission");
            Require(queued.RootElement.GetProperty("requestId").GetInt64() == 72 && queued.RootElement.GetProperty("error").GetString() == "cancelled", "queued request drained after in-flight cancellation");
            using var ended = await ReadKind(guest, "ended");
            deadline = Environment.TickCount64 + 10000;
            TransitionResult result;
            do { result = await guest.HostAsync(0, false); if (result == TransitionResult.Busy) await Task.Delay(5); }
            while (result == TransitionResult.Busy && Environment.TickCount64 < deadline);
            Require(result == TransitionResult.Started, $"drained retained mailbox must not block retry: {result}");
        }
        finally { sendLock.Release(); }
    }

    public static async Task PreviewHostChurn()
    {
        await using var host = new IngameSessionController();
        Require(await host.HostAsync(0, false) == TransitionResult.Started, "host");
        var ids = new HashSet<Guid>();
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(60));
        for (int i = 0; i < 257; i++)
        {
            await using (var guest = await SessionClient.ConnectAsync(System.Net.IPAddress.Loopback, host.Status.BoundPort, "", timeout.Token))
            {
                Require(ids.Add(guest.PlayerId), "unique player");
                while (host.Status.PeerCount != 1) { timeout.Token.ThrowIfCancellationRequested(); await Task.Delay(1); }
            }
            while (host.Status.PeerCount != 0) { timeout.Token.ThrowIfCancellationRequested(); await Task.Delay(1); }
            Require(host.Status.State == IngameState.Hosting, $"host failed at connection {i + 1}: {host.Status.State}");
        }
        // Give both lifecycle readers time to catch up without polling the gameplay ABI.
        await Task.Delay(200);
        Require(host.Status.State == IngameState.Hosting, "host must survive lifecycle delivery");
        var events = new Dictionary<Guid, List<string>>();
        var buffer = new byte[GameplayMailbox.MaxEventBytes + 1];
        var deadline = Environment.TickCount64 + 10000;
        while (events.Sum(e => e.Value.Count) < 514 && Environment.TickCount64 < deadline)
        {
            var n = host.PollGameplay(buffer);
            if (n == 0) { await Task.Delay(5); continue; }
            Require(n > 0, "poll");
            using var frame = JsonDocument.Parse(buffer.AsMemory(0, n));
            var player = frame.RootElement.GetProperty("playerId").GetGuid();
            var kind = frame.RootElement.GetProperty("kind").GetString()!;
            if (!events.TryGetValue(player, out var kinds)) events[player] = kinds = [];
            kinds.Add(kind);
        }
        Require(ids.SetEquals(events.Keys) && events.Values.All(k => k.SequenceEqual(new[] { "joined", "left" })), "all TCP lifecycle events retained in per-player order");
    }

    public static Task TerminalAfterReliable()
    {
        var box = new GameplayMailbox(1);
        Require(box.Reserve(1) == 0, "reservation");
        box.End("transport", final: false);
        Require(box.Poll(Span<byte>.Empty) == 0, "intermediate pump error must not publish terminal");
        box.End("transport");
        Require(box.Poll(Span<byte>.Empty) == 0, "terminal must wait for in-flight request");
        box.Add("submission", Guid.Empty, requestId: 1, error: "cancelled");
        var buffer = new byte[1024];
        foreach (var kind in new[] { "submission", "ended" })
        {
            var n = box.Poll(buffer);
            Require(n > 0, "event");
            using var frame = JsonDocument.Parse(buffer.AsMemory(0, n));
            Require(frame.RootElement.GetProperty("kind").GetString() == kind, "terminal ordering");
        }
        return Task.CompletedTask;
    }

    public static Task MailboxFairness()
    {
        var box = new GameplayMailbox(1);
        var player = Guid.NewGuid();
        Require(box.Reserve(7) == NativeExports.Accepted, "reserve request");
        Require(box.Reserve(7) == NativeExports.InvalidArgument, "duplicate pending id");
        box.Add("joined", player);
        box.Add("input", player, input: new Wsm.Core.PlayerInput(player, 0, 0));
        for (var i = 0; i < 8; i++)
        {
            var length = box.Poll(Span<byte>.Empty);
            Require(length > 0, "expected pending event");
            Require(box.Poll(new byte[1]) == NativeExports.InsufficientCapacity, "small poll must not consume");
            var buffer = new byte[GameplayMailbox.MaxEventBytes + 1];
            var written = box.Poll(buffer);
            Require(written > 0, "consume event");
            using var doc = JsonDocument.Parse(buffer.AsMemory(0, written));
            if (doc.RootElement.GetProperty("kind").GetString() == "joined") return Task.CompletedTask;
            box.Add("input", player, input: new Wsm.Core.PlayerInput(player, 0, 0));
        }
        throw new InvalidOperationException("joined starved by continuously replenished input");
    }
    public static async Task SubmissionResults()
    {
        await using var host = new IngameSessionController();
        await using var guest = new IngameSessionController();
        Require(await host.HostAsync(0, false) == TransitionResult.Started, "host");
        Require(await guest.JoinAsync("127.0.0.1", host.Status.BoundPort) == TransitionResult.Started, "guest");
        using var joined = await ReadKind(host, "joined");
        for (int i = 1; i <= 3; i++)
        {
            var tick = i == 2 ? 1 : i;
            var json = $"{{\"kind\":\"snapshot\",\"requestId\":{i},\"snapshot\":{{\"tick\":{tick},\"entities\":[],\"enemies\":[]}}}}";
            Require(host.SubmitGameplay(host.GameplayEpoch, Encoding.UTF8.GetBytes(json)) == 0, "request queued");
        }
        while (true)
        {
            using var snapshot = await ReadKind(guest, "snapshot");
            if (snapshot.RootElement.GetProperty("snapshot").GetProperty("tick").GetInt64() == 3) break;
        }
        for (int i = 1; i <= 3; i++)
        {
            using var result = await ReadKind(host, "submission");
            Require(result.RootElement.GetProperty("requestId").GetInt64() == i, "completion correlation/order");
            Require(result.RootElement.TryGetProperty("error", out _) == (i == 2), "per-request outcome");
        }
    }
    public static Task DuplicatePendingRequest()
    {
        var box = new GameplayMailbox(1);
        Require(box.Reserve(7) == NativeExports.Accepted, "first pending id");
        Require(box.Reserve(7) == NativeExports.InvalidArgument, "duplicate pending id");
        box.Add("submission", Guid.Empty, requestId: 7);
        var buffer = new byte[GameplayMailbox.MaxEventBytes + 1];
        var length = box.Poll(buffer);
        Require(length > 0, "completion available");
        using var doc = JsonDocument.Parse(buffer.AsMemory(0, length));
        Require(doc.RootElement.GetProperty("requestId").GetInt64() == 7, "completion id");
        Require(box.Reserve(7) == NativeExports.Accepted, "id reusable after completion");
        box.Clear();
        Require(box.Reserve(7) == NativeExports.Unavailable, "cleared mailbox rejects new request");
        return Task.CompletedTask;
    }
    public static async Task InputRoundtrip()
    {
        await using var host = new IngameSessionController();
        await using var guest = new IngameSessionController();
        Require(await host.HostAsync(0, false) == TransitionResult.Started, "host");
        Require(await guest.JoinAsync("127.0.0.1", host.Status.BoundPort) == TransitionResult.Started, "join");
        var epoch = guest.GameplayEpoch;
        Require(epoch > 0 && guest.GameplayPlayerId != Guid.Empty, "guest session identity");
        Require(guest.SubmitGameplay(epoch, Encoding.UTF8.GetBytes("{\"kind\":\"input\",\"requestId\":1,\"x\":0.25,\"y\":-0.5}")) == 0, "input queue");
        using var received = await ReadKind(host, "input");
        var frame = received.RootElement;
        Require(frame.GetProperty("epoch").GetInt32() == host.GameplayEpoch, "receiver epoch");
        Require(frame.GetProperty("playerId").GetGuid() == guest.GameplayPlayerId, "authenticated input owner");
        var input = frame.GetProperty("input");
        Require(input.GetProperty("x").GetSingle() == 0.25f && input.GetProperty("y").GetSingle() == -0.5f, "actual input vector");
        Require(frame.GetProperty("ageMilliseconds").GetInt64() >= 0, "receiver freshness");
    }
    public static async Task WorldAndPersonalRewards()
    {
        await using var host = new IngameSessionController();
        await using var guest = new IngameSessionController();
        await using var other = new IngameSessionController();
        Require(await host.HostAsync(0, false) == TransitionResult.Started, "host");
        Require(await guest.JoinAsync("127.0.0.1", host.Status.BoundPort) == TransitionResult.Started, "guest");
        Require(await other.JoinAsync("127.0.0.1", host.Status.BoundPort) == TransitionResult.Started, "other");
        var world = "{\"kind\":\"snapshot\",\"requestId\":1,\"snapshot\":{\"tick\":1,\"entities\":[{\"id\":\"actual-test-entity\",\"x\":12.5,\"y\":-4.25,\"alive\":true}],\"enemies\":[]}}";
        Require(host.SubmitGameplay(host.GameplayEpoch, Encoding.UTF8.GetBytes(world)) == 0, "world queue");
        using var first = await ReadKind(guest, "snapshot");
        using var second = await ReadKind(other, "snapshot");
        Require(first.RootElement.GetProperty("snapshot").GetRawText() == second.RootElement.GetProperty("snapshot").GetRawText(), "same actual received world snapshot");
        Require(first.RootElement.GetProperty("snapshot").GetProperty("entities")[0].GetProperty("x").GetSingle() == 12.5f, "world position survives TCP and mailbox");
        var receipt = Guid.NewGuid();
        var reward = $"{{\"kind\":\"reward\",\"requestId\":2,\"reward\":{{\"receiptId\":\"{receipt}\",\"recipientId\":\"{guest.GameplayPlayerId}\",\"kind\":0,\"amount\":9}}}}";
        Require(host.SubmitGameplay(host.GameplayEpoch, Encoding.UTF8.GetBytes(reward)) == 0, "personal reward queue");
        using var received = await ReadKind(guest, "reward");
        Require(received.RootElement.GetProperty("reward").GetProperty("receiptId").GetGuid() == receipt, "actual receipt identity");
        Require(received.RootElement.GetProperty("reward").GetProperty("amount").GetInt32() == 9, "personal reward value");
        await Task.Delay(100);
        Require(other.PollGameplay(Span<byte>.Empty) == 0, "personal XP was not delivered to other player");
        Require(guest.SubmitGameplay(guest.GameplayEpoch, Encoding.UTF8.GetBytes("{\"kind\":\"upgrade\",\"requestId\":3,\"level\":2,\"choice\":\"fixture-personal-choice\"}")) == 0, "upgrade queued");
        using var decision = await ReadKind(host, "upgrade");
        Require(decision.RootElement.GetProperty("upgrade").GetProperty("choice").GetString() == "fixture-personal-choice", "independent decision survives TCP");
        Require(decision.RootElement.GetProperty("playerId").GetGuid() == guest.GameplayPlayerId, "decision owner");
    }
    public static async Task CapacityAndEpoch()
    {
        await using var host = new IngameSessionController();
        await using var guest = new IngameSessionController();
        Require(await host.HostAsync(0, false) == TransitionResult.Started, "host");
        Require(await guest.JoinAsync("127.0.0.1", host.Status.BoundPort) == TransitionResult.Started, "guest");
        using var joined = await ReadKind(host, "joined");
        var old = guest.GameplayEpoch;
        Require(guest.SubmitGameplay(old, Encoding.UTF8.GetBytes("{\"kind\":\"upgrade\",\"requestId\":1,\"level\":2,\"choice\":\"capacity-choice\"}")) == 0, "choice");
        var deadline = Environment.TickCount64 + 10000;
        int length;
        while ((length = host.PollGameplay(Span<byte>.Empty)) == 0 && Environment.TickCount64 < deadline) await Task.Delay(5);
        Require(length > 0, "event available");
        var small = Enumerable.Repeat((byte)0x5a, length).ToArray();
        Require(host.PollGameplay(small) == NativeExports.InsufficientCapacity && small.All(b => b == 0x5a), "small buffer unchanged and not consumed");
        using var retained = await ReadKind(host, "upgrade");
        Require(retained.RootElement.GetProperty("upgrade").GetProperty("choice").GetString() == "capacity-choice", "event retained");
        await guest.LeaveAsync();
        Require(guest.PollGameplay(Span<byte>.Empty) == 0 && guest.GameplayEpoch == 0, "leave clears epoch and abandoned events");
        Require(await guest.JoinAsync("127.0.0.1", host.Status.BoundPort) == TransitionResult.Started, "rejoin");
        Require(guest.GameplayEpoch > old, "new epoch");
        Require(guest.SubmitGameplay(old, Encoding.UTF8.GetBytes("{\"kind\":\"input\",\"requestId\":2,\"x\":0,\"y\":0}")) == NativeExports.Unavailable, "old epoch cannot submit into new session");
    }
    public static async Task Validation()
    {
        await using var host = new IngameSessionController();
        await using var guest = new IngameSessionController();
        Require(await host.HostAsync(0, false) == TransitionResult.Started, "host");
        Require(await guest.JoinAsync("127.0.0.1", host.Status.BoundPort) == TransitionResult.Started, "guest");
        foreach (var malformed in new[] {
            "{}", "null", "[]", "{\"kind\":\"input\",\"requestId\":1}",
            "{\"kind\":\"input\",\"requestId\":1,\"x\":2,\"y\":0}",
            "{\"kind\":\"input\",\"requestId\":1,\"x\":1,\"y\":1}",
            "{\"kind\":\"input\",\"requestId\":1,\"x\":0,\"x\":1,\"y\":0}",
            "{\"kind\":\"input\",\"requestId\":1,\"x\":0,\"y\":0,\"playerId\":\"00000000-0000-0000-0000-000000000001\"}",
            "{\"kind\":\"upgrade\",\"requestId\":1,\"level\":0,\"choice\":\"a\"}",
            "{\"kind\":\"snapshot\",\"requestId\":1,\"snapshot\":{\"tick\":1,\"entities\":[]}}" })
            Require(guest.SubmitGameplay(guest.GameplayEpoch, Encoding.UTF8.GetBytes(malformed)) == NativeExports.InvalidArgument, "invalid/unauthorized payload rejected: " + malformed);
        Require(host.SubmitGameplay(host.GameplayEpoch, Encoding.UTF8.GetBytes("{\"kind\":\"input\",\"requestId\":1,\"x\":0,\"y\":0}")) == NativeExports.InvalidArgument, "host cannot forge guest command");
        Require(host.SubmitGameplay(host.GameplayEpoch, Encoding.UTF8.GetBytes("{\"kind\":\"snapshot\",\"requestId\":1,\"snapshot\":{\"tick\":1,\"entities\":null}}")) == NativeExports.InvalidArgument, "null entities rejected");
        Require(guest.SubmitGameplay(guest.GameplayEpoch, Encoding.UTF8.GetBytes("{\"kind\":\"input\",\"requestId\":2,\"x\":0,\"y\":0}")) == 0, "valid retry");
        using var result = await ReadKind(host, "input");
    }
    public static async Task RewardsSurviveStalledPump()
    {
        await using var host = new IngameSessionController();
        await using var guest = new IngameSessionController();
        Require(await host.HostAsync(0, false) == TransitionResult.Started, "host");
        Require(await guest.JoinAsync("127.0.0.1", host.Status.BoundPort) == TransitionResult.Started, "guest");
        var ids = new HashSet<Guid>();
        for (var i = 0; i < 129; i++)
        {
            var id = Guid.NewGuid(); ids.Add(id);
            var json = $"{{\"kind\":\"reward\",\"requestId\":{i + 1},\"reward\":{{\"receiptId\":\"{id}\",\"recipientId\":\"{guest.GameplayPlayerId}\",\"kind\":0,\"amount\":1}}}}";
            Require(host.SubmitGameplay(host.GameplayEpoch, Encoding.UTF8.GetBytes(json)) == 0, "reward queue");
            using var accepted = await ReadKind(host, "submission");
            Require(!accepted.RootElement.TryGetProperty("error", out _), "host accepted reward");
        }
        Require(guest.PendingGameplayRewards == 129, "every Core-accepted receipt survives the bounded channel");
        for (var i = 0; i < 129; i++)
        {
            using var receipt = await ReadKind(guest, "reward");
            Require(ids.Remove(receipt.RootElement.GetProperty("reward").GetProperty("receiptId").GetGuid()), "unique retained reward");
        }
        Require(ids.Count == 0, "all 129 stalled-pump rewards delivered");
    }
    public static async Task StalledBridgeReconcilesLedgerAfterHostLoss()
    {
        await using var host = new IngameSessionController();
        await using var guest = new IngameSessionController();
        Require(await host.HostAsync(0, false) == TransitionResult.Started, "host");
        Require(await guest.JoinAsync("127.0.0.1", host.Status.BoundPort) == TransitionResult.Started, "guest");
        var sessionField = typeof(IngameSessionController).GetField("active",
            System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance)!;
        var client = (SessionClient)sessionField.GetValue(guest)!.GetType().GetProperty("Client")!.GetValue(sessionField.GetValue(guest)!)!;

        // Delay bridge reads while the transport receives a burst larger than the old queue.
        var stalled = true;
        guest.StallGameplayChannelForTests = name => name == "rewards" && stalled
            ? TimeSpan.FromMilliseconds(500) : TimeSpan.Zero;

        var requestId = 0;
        var total = 200;
        var buffer = new byte[GameplayMailbox.MaxEventBytes + 1];
        var pushed = 0;
        var pushDeadline = Environment.TickCount64 + 15000;
        while (pushed < total && Environment.TickCount64 < pushDeadline)
        {
            for (var i = 0; i < 4 && pushed < total; i++)
            {
                var id = Guid.NewGuid();
                var json = $"{{\"kind\":\"reward\",\"requestId\":{Interlocked.Increment(ref requestId)},\"reward\":{{\"receiptId\":\"{id}\",\"recipientId\":\"{guest.GameplayPlayerId}\",\"kind\":0,\"amount\":1}}}}";
                if (host.SubmitGameplay(host.GameplayEpoch, Encoding.UTF8.GetBytes(json)) == 0) pushed++;
            }
            while (host.PollGameplay(Span<byte>.Empty) > 0) host.PollGameplay(buffer);
            await Task.Delay(2);
        }
        Require(pushed == total, $"host pushed only {pushed} of {total} rewards");

        var ledgerDeadline = Environment.TickCount64 + 12000;
        while (client.ReceivedRewardCount < total && Environment.TickCount64 < ledgerDeadline) await Task.Delay(20);
        Require(client.ReceivedRewardCount == total && client.Progression.Xp == total && guest.Status.State == IngameState.Connected,
            $"guest must stay connected and receive all {total} XP; got {client.ReceivedRewardCount}");
        stalled = false;

        // Once the consumer resumes, every receipt Core accepted has to arrive exactly once.
        var delivered = new HashSet<Guid>();
        var drainDeadline = Environment.TickCount64 + 30000;
        while (delivered.Count < total && Environment.TickCount64 < drainDeadline)
        {
            if (guest.PollGameplay(Span<byte>.Empty) == 0) { await Task.Delay(5); continue; }
            var written = guest.PollGameplay(buffer);
            if (written <= 0) { await Task.Delay(5); continue; }
            using var frame = JsonDocument.Parse(buffer.AsMemory(0, written));
            if (frame.RootElement.GetProperty("kind").GetString() != "reward") continue;
            Require(delivered.Add(frame.RootElement.GetProperty("reward").GetProperty("receiptId").GetGuid()),
                "each receipt is delivered exactly once");
        }
        var finalLedger = client.ReceivedRewards.Select(r => r.ReceiptId).ToHashSet();
        Require(delivered.SetEquals(finalLedger),
            $"{finalLedger.Count - delivered.Count} Core-accepted receipts never reached the consumer " +
            $"(ledger={finalLedger.Count} delivered={delivered.Count})");

        // And after host loss the retained mailbox must still hold everything not yet polled.
        await host.LeaveAsync();
        var lossDeadline = Environment.TickCount64 + 10000;
        while (guest.Status.State != IngameState.Disconnected && Environment.TickCount64 < lossDeadline) await Task.Delay(5);
        Require(guest.Status.State == IngameState.Disconnected && guest.GameplayEpoch > 0, "epoch retained after host loss");
        Require(guest.PendingGameplayRewards == finalLedger.Count - delivered.Count,
            $"retained mailbox holds {guest.PendingGameplayRewards}, expected {finalLedger.Count - delivered.Count}");
        await guest.LeaveAsync();
    }
    public static async Task RewardsSurviveUnpolledBurstAndHostLoss()
    {
        await using var host = new IngameSessionController();
        await using var guest = new IngameSessionController();
        Require(await host.HostAsync(0, false) == TransitionResult.Started, "host");
        Require(await guest.JoinAsync("127.0.0.1", host.Status.BoundPort) == TransitionResult.Started, "guest");
        var ids = new HashSet<Guid>();
        for (int i = 0; i < 140; ++i)
        {
            var id = Guid.NewGuid(); ids.Add(id);
            var json = $"{{\"kind\":\"reward\",\"requestId\":{i + 1},\"reward\":{{\"receiptId\":\"{id}\",\"recipientId\":\"{guest.GameplayPlayerId}\",\"kind\":0,\"amount\":1}}}}";
            Require(host.SubmitGameplay(host.GameplayEpoch, Encoding.UTF8.GetBytes(json)) == 0, "reward queue");
            using var accepted = await ReadKind(host, "submission");
            Require(!accepted.RootElement.TryGetProperty("error", out _), "reward send accepted without error");
        }
        var deliveryDeadline = Environment.TickCount64 + 10000;
        while (guest.PendingGameplayRewards < 140 && Environment.TickCount64 < deliveryDeadline) await Task.Delay(5);
        Require(guest.PendingGameplayRewards == 140, "all receipts reached receiver before host close; enqueue is not delivery");
        await host.LeaveAsync();
        var deadline = Environment.TickCount64 + 10000;
        while (guest.Status.State != IngameState.Disconnected && Environment.TickCount64 < deadline) await Task.Delay(5);
        Require(guest.Status.State == IngameState.Disconnected && guest.GameplayEpoch > 0, "host loss retains inbox epoch");
        for (int i = 0; i < 140; ++i)
        {
            using var receipt = await ReadKind(guest, "reward");
            Require(ids.Remove(receipt.RootElement.GetProperty("reward").GetProperty("receiptId").GetGuid()), "unique retained reward");
        }
        Require(ids.Count == 0, "all accepted rewards retained beyond Core channel capacity and host loss");
    }
    public static async Task InputFollowsJoinUnderSlowLifecycleReader()
    {
        await using var host = new IngameSessionController();
        await using var guest = new IngameSessionController();
        Require(await host.HostAsync(0, false) == TransitionResult.Started, "host");

        // Hold the host's join reader while the command reader keeps running. The two readers are
        // independent, so this is the exact race the bridge must not expose to the game.
        var stalled = true;
        host.StallGameplayChannelForTests = name => name == "joined" && stalled
            ? TimeSpan.FromMilliseconds(400) : TimeSpan.Zero;

        Require(await guest.JoinAsync("127.0.0.1", host.Status.BoundPort) == TransitionResult.Started, "guest");
        Require(guest.SubmitGameplay(guest.GameplayEpoch,
            Encoding.UTF8.GetBytes("{\"kind\":\"upgrade\",\"requestId\":1,\"level\":2,\"choice\":\"join-order-choice\"}")) == 0, "upgrade queued");
        await Task.Delay(1200);
        stalled = false;

        var kinds = await CollectKinds(host, TimeSpan.FromSeconds(3));
        var joined = kinds.IndexOf("joined");
        var upgrade = kinds.IndexOf("upgrade");
        Require(joined >= 0, $"the join was never reported to the game (events: {string.Join(",", kinds)})");
        Require(upgrade >= 0, $"the upgrade was never reported (events: {string.Join(",", kinds)})");
        Require(joined < upgrade, $"input overtook its own join: {string.Join(",", kinds)}");
        await guest.LeaveAsync();
        await Task.Delay(200);
    }
    public static async Task JoinPrecedesLeaveUnderSlowLifecycleReader()
    {
        await using var host = new IngameSessionController();
        Require(await host.HostAsync(0, false) == TransitionResult.Started, "host");

        var stalled = true;
        host.StallGameplayChannelForTests = name => name == "joined" && stalled
            ? TimeSpan.FromMilliseconds(1500) : TimeSpan.Zero;

        var guest = new IngameSessionController();
        Require(await guest.JoinAsync("127.0.0.1", host.Status.BoundPort) == TransitionResult.Started, "guest");
        var playerId = guest.GameplayPlayerId;
        await Task.Delay(200);
        await guest.LeaveAsync();
        await Task.Delay(2000);
        stalled = false;

        var events = new List<(string Kind, Guid Player)>();
        var buffer = new byte[GameplayMailbox.MaxEventBytes + 1];
        var deadline = Environment.TickCount64 + 3000;
        while (Environment.TickCount64 < deadline)
        {
            var length = host.PollGameplay(Span<byte>.Empty);
            if (length <= 0) { await Task.Delay(5); continue; }
            var written = host.PollGameplay(buffer);
            if (written <= 0) { await Task.Delay(5); continue; }
            using var frame = JsonDocument.Parse(buffer.AsMemory(0, written));
            events.Add((frame.RootElement.GetProperty("kind").GetString()!,
                frame.RootElement.GetProperty("playerId").GetGuid()));
        }
        var kinds = events.Select(e => e.Kind).ToList();
        var joined = kinds.IndexOf("joined");
        var left = kinds.IndexOf("left");
        Require(joined >= 0,
            $"a player who connected must be reported joined, even if it left first (events: {string.Join(",", kinds)})");
        Require(left >= 0, $"the leave was never reported (events: {string.Join(",", kinds)})");
        Require(joined < left, $"leave was reported before join: {string.Join(",", kinds)}");
        Require(events[firstIndexOf(kinds, "joined")].Player == playerId, "join identity matches the authenticated player");
        await host.LeaveAsync();

        static int firstIndexOf(List<string> values, string value) => values.IndexOf(value);
    }
    internal static async Task<List<string>> CollectKinds(IngameSessionController controller, TimeSpan window)
    {
        var kinds = new List<string>();
        var buffer = new byte[GameplayMailbox.MaxEventBytes + 1];
        var deadline = Environment.TickCount64 + (long)window.TotalMilliseconds;
        while (Environment.TickCount64 < deadline)
        {
            var length = controller.PollGameplay(Span<byte>.Empty);
            if (length <= 0) { await Task.Delay(5); continue; }
            var written = controller.PollGameplay(buffer);
            if (written <= 0) { await Task.Delay(5); continue; }
            using var frame = JsonDocument.Parse(buffer.AsMemory(0, written));
            kinds.Add(frame.RootElement.GetProperty("kind").GetString()!);
        }
        return kinds;
    }
    internal static async Task<JsonDocument> ReadKind(IngameSessionController controller, string kind)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        while (true)
        {
            timeout.Token.ThrowIfCancellationRequested();
            var length = controller.PollGameplay(Span<byte>.Empty);
            if (length > 0)
            {
                var buffer = new byte[length + 1];
                var result = controller.PollGameplay(buffer);
                if (result == NativeExports.InsufficientCapacity) continue;
                Require(result > 0 && buffer[result] == 0, "event buffer contract");
                var frame = JsonDocument.Parse(buffer.AsMemory(0, result));
                if (frame.RootElement.GetProperty("kind").GetString() == kind) return frame;
                frame.Dispose();
            }
            await Task.Delay(5, timeout.Token);
        }
    }
    internal static void Require(bool value, string message)
    { if (!value) throw new InvalidOperationException(message); }
}
