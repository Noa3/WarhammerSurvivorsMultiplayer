using System.Net;
using Wsm.Core;

internal static class BotRegression
{
    private static void Check(bool value, string message) { if (!value) throw new Exception(message); }

    public static async Task Run()
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        await using var host = new SessionHost(new(1, DeathMode.AllDead, 1, BotDifficulty.Hard), "x");
        host.Start();
        await using var client = await SessionClient.ConnectAsync(IPAddress.Loopback, host.Port, "x", timeout.Token);
        await host.Joined.ReadAsync(timeout.Token);
        host.RegisterSimulatedBot(new("bot", 0, 0, true), weaponRange: 5, maxStep: 1);
        host.Encounter.RegisterEnemy("orc", 10);
        async Task<WorldEntity> Step(long tick, params WorldEntity[] entities)
        {
            await host.PublishSnapshotAsync(new(tick, entities), timeout.Token);
            var received = await client.Snapshots.ReadAsync(timeout.Token);
            Check(received.Tick == tick && received.Entities.Count(e => e.Id == "bot") == 1, "accepted tick must publish the simulated bot");
            var actor = received.Entities.Single(e => e.Id == "bot");
            var step = host.LatestBotSteps.Single();
            Check(step.Tick == tick && step.Actor == actor, "host intent and client position must describe the same step");
            return actor;
        }

        var approach = await Step(1, new WorldEntity("orc", 8, 0, true));
        Check(approach.X == 1 && approach.Y == 0 && host.LatestBotSteps.Single().AttackTarget is null, "approach, no out-of-range attack");
        var hold = await Step(2, new WorldEntity("orc", 6, 0, true));
        Check(hold == approach && host.LatestBotSteps.Single().AttackTarget == "orc", "hold and request attack at inclusive weapon range");
        var retreat = await Step(3, new WorldEntity("orc", 2, 0, true), new WorldEntity("xp", 2, 0, true));
        Check(retreat.X < hold.X && host.LatestBotSteps.Single().AttackTarget == "orc", "retreat and request in-range attack, not an XP detour");
        Check(host.Encounter.Snapshot().Single().Hp == 10, "an attack intent is not a validated hit");
        host.Encounter.ApplyValidatedHit(Guid.NewGuid(), "orc", 10);
        var idle = await Step(4, new WorldEntity("orc", 2, 0, true), new WorldEntity("xp", 8, 0, true));
        Check(idle == retreat && host.LatestBotSteps.Single().AttackTarget is null && client.Progression.Xp == 0, "no pursuit of dead enemies or unclassified entities and no invented XP");
        ObservedPositions();
        ObservedGoalsAndDifficulty();
        ObservedBounds();
        await XpSafety();
        await Difficulty();
        await FailedTick();
        await FlankedRetreat();
        await PersonalGoal();
    }

    private static void ObservedPositions()
    {
        WorldEntity a = new("a", 0, 0, true), b = new("b", 12, 0, true);
        WorldEntity[] enemies = [new("e", 6, 0, true)];
        for (long tick = 0; tick < 4; tick++)
        {
            // Both actors are blocked: the requested step would reach range, but neither moved.
            Check(SessionHost.PlanObservedBot(a, 5, 1, BotDifficulty.Hard, tick, enemies, null) is
                { X: 1, Y: 0, AttackTarget: null }, "blocked bot must approach without attacking from a predicted position");
            Check(SessionHost.PlanObservedBot(b, 5, 1, BotDifficulty.Hard, tick, enemies, null) is
                { X: -1, Y: 0, AttackTarget: null }, "second bot must use its own observed position");
        }
        Check(SessionHost.PlanObservedBot(a with { X = 1 }, 5, 1, BotDifficulty.Hard, 4, enemies, null) is
            { X: 0, Y: 0, AttackTarget: "e" }, "attack only once observed at inclusive range");
        Check(SessionHost.PlanObservedBot(a with { X = 1, Alive = false }, 5, 1, BotDifficulty.Hard, 5, enemies, null) is
            { X: 0, Y: 0, AttackTarget: null }, "dead actor must not act");
    }

    private static void ObservedGoalsAndDifficulty()
    {
        WorldEntity actor = new("a", 0, 0, true), enemy = new("enemy", 4.5f, 0, true), pickup = new("xp", 3, 4, true);
        foreach (var (difficulty, x) in new[] { (BotDifficulty.Easy, 0f), (BotDifficulty.Normal, -0.25f), (BotDifficulty.Hard, -1f) })
            Check(SessionHost.PlanObservedBot(actor, 10, 2, difficulty, 1, [enemy], null) == new BotCommand(x, 0, enemy.Id),
                $"{difficulty}: normalize spacing by maxStep, not by planned distance");
        var deadEnemy = enemy with { Alive = false };
        var move = SessionHost.PlanObservedBot(actor, 10, 2, BotDifficulty.Hard, 1, [deadEnemy], pickup);
        Check(Math.Abs(move.X - 0.6f) < 1e-6 && Math.Abs(move.Y - 0.8f) < 1e-6 &&
            (double)move.X * move.X + (double)move.Y * move.Y <= 1 && move.AttackTarget is null,
            "safe personal pickup: bounded diagonal input, not an attack");
        Check(SessionHost.PlanObservedBot(actor, 10, 2, BotDifficulty.Hard, 1, [deadEnemy], pickup with { Alive = false }) == default,
            "dead enemy and inactive pickup must be ignored");
        Check(SessionHost.PlanObservedBot(actor, 5, 2, BotDifficulty.Hard, 1, [enemy], pickup with { X = 9, Y = 0 }) is
            { X: 0, Y: 0, AttackTarget: "enemy" }, "do not pursue a personal pickup through an enemy");
    }

    private static void ObservedBounds()
    {
        WorldEntity actor = new("bot", 0, 0, true), enemy = new("e", 2, 0, true);
        var enemies = Enumerable.Range(0, 2048).Select(i => enemy with { Id = $"e{i}" }).ToArray();
        Check(SessionHost.PlanObservedBot(actor, 2, 1, BotDifficulty.Normal, long.MaxValue, enemies, null).AttackTarget == "e0",
            "2048 enemies accepted; ordinal tie break");
        BotCommand Plan(WorldEntity[] list, WorldEntity? pickup = null, float range = 2, float step = 1,
            long tick = 0, BotDifficulty difficulty = BotDifficulty.Hard) =>
            SessionHost.PlanObservedBot(actor, range, step, difficulty, tick, list, pickup);
        static void Reject(Action call, string label)
        {
            try { call(); } catch (ArgumentException) { return; }
            throw new Exception($"Invalid observed bot input accepted: {label}");
        }
        Reject(() => Plan([.. enemies, enemy]), "2049 enemies");
        Reject(() => Plan(null!), "null list");
        Reject(() => Plan([null!]), "null enemy");
        Reject(() => Plan([enemy, enemy with { Alive = false }]), "duplicate enemy");
        Reject(() => Plan([actor]), "actor classified as enemy");
        Reject(() => Plan([enemy], enemy), "enemy classified as pickup");
        Reject(() => Plan([], actor), "actor classified as pickup");
        Reject(() => Plan([enemy with { Id = " " }]), "blank ID");
        Reject(() => Plan([], enemy with { Id = new string('a', 129) }), "oversized pickup ID");
        Reject(() => Plan([enemy with { X = float.NaN, Alive = false }]), "nonfinite dead enemy");
        Reject(() => Plan([], enemy with { Y = float.NegativeInfinity }), "nonfinite pickup");
        Reject(() => Plan([], range: 0), "zero range");
        Reject(() => Plan([], step: 0), "zero maxStep");
        Reject(() => Plan([], step: float.PositiveInfinity), "nonfinite maxStep");
        Reject(() => Plan([], difficulty: (BotDifficulty)99), "unknown difficulty");
        Reject(() => Plan([], tick: -1), "negative tick");
        actor = actor with { X = -float.MaxValue };
        Reject(() => Plan([enemy with { X = actor.X }]), "retreat beyond minimum coordinate");
        Check(Plan([enemy with { X = float.MaxValue }]) is { X: 1, Y: 0, AttackTarget: null },
            "wide distance and a sub-coordinate-precision stride must still give finite input");
    }

    private static async Task XpSafety()
    {
        await using var host = new SessionHost(new(1, DeathMode.AllDead, 1, BotDifficulty.Hard), "x");
        host.RegisterSimulatedBot(new("b", 0, 0, true), 10, 2);
        var goal = new Dictionary<string, string> { ["b"] = "drop" };
        WorldEntity drop = new("drop", 0, 8, true);
        await host.PublishSnapshotWithBotGoalsAsync(new(1, [drop]), goal);
        Check(host.LatestBotSteps.Single().Actor.Y == 2, "explicit safe XP goal must advance the bot");
        host.Encounter.RegisterEnemy("e", 1);
        WorldEntity threat = new("e", 0, 10, true);
        await host.PublishSnapshotWithBotGoalsAsync(new(2, [threat, drop with { Y = 22 }]), goal);
        Check(host.LatestBotSteps.Single() is { Actor.Y: 2, AttackTarget: "e" }, "unsafe XP route through enemy must lose to safe in-range attack");
        await host.PublishSnapshotWithBotGoalsAsync(new(3, [threat with { Y = 3 }, drop with { X = 5 }]), goal);
        Check(host.LatestBotSteps.Single().Actor.Y < 2, "retreat takes precedence over explicit XP");
        await host.PublishSnapshotAsync(new(4, [drop]));
        Check(host.LatestBotSteps.Single().Actor.Y == 0, "previous hint must not persist");
    }

    private static async Task Difficulty()
    {
        foreach (var (difficulty, expectedX) in new[] { (BotDifficulty.Easy, 0f), (BotDifficulty.Normal, -0.5f), (BotDifficulty.Hard, -1f) })
        {
            await using var host = new SessionHost(new(1, DeathMode.AllDead, 1, difficulty), "x");
            host.RegisterSimulatedBot(new("b", 0, 0, true), 10, 1);
            host.Encounter.RegisterEnemy("e", 1);
            await host.PublishSnapshotAsync(new(1, [new("e", 4.5f, 0, true)]));
            Check(host.LatestBotSteps.Single().Actor.X == expectedX && host.LatestBotSteps.Single().AttackTarget == "e", $"{difficulty}: spacing must change without extending range");
        }
    }

    private static async Task FailedTick()
    {
        await using var host = new SessionHost(new(1, DeathMode.AllDead, 1, BotDifficulty.Hard), "x");
        host.RegisterSimulatedBot(new("b", 0, 0, true), 1, 1);
        host.Encounter.RegisterEnemy("e", 1);
        WorldEntity enemy = new("e", 5, 0, true);
        WorldEntity oversized = new(new string('é', 128), 0, 0, true);
        try { await host.PublishSnapshotAsync(new(1, [enemy, .. Enumerable.Repeat(oversized, 1000)])); throw new Exception("oversize accepted"); }
        catch (InvalidDataException) { }
        Check(host.LatestBotSteps.Length == 0, "failed encoding advanced bot");
        await host.PublishSnapshotAsync(new(1, [enemy]));
        Check(host.LatestBotSteps.Single().Actor.X == 1, "same-tick retry performed more than one step");
    }

    private static async Task FlankedRetreat()
    {
        await using var host = new SessionHost(new(1, DeathMode.AllDead, 1, BotDifficulty.Hard), "x");
        host.RegisterSimulatedBot(new("b", 0, 0, true), 5, 1);
        host.Encounter.RegisterEnemy("left", 1);
        host.Encounter.RegisterEnemy("right", 1);
        await host.PublishSnapshotAsync(new(1, [new("right", 1, 0, true), new("left", -1, 0, true)]));
        Check(host.LatestBotSteps.Single() is { Actor.X: 0, AttackTarget: "left" } step && Math.Abs(step.Actor.Y) == 1,
            "flanked bot must escape sideways rather than retreat into another enemy; ordinal tie break");
    }

    private static async Task PersonalGoal()
    {
        await using var host = new SessionHost(new(1, DeathMode.AllDead, 2, BotDifficulty.Hard), "x");
        host.RegisterSimulatedBot(new("a", 0, 0, true), 1, 1);
        host.RegisterSimulatedBot(new("A", 0, 0, true), 5, 1);
        await host.PublishSnapshotWithBotGoalsAsync(new(1, [new("drop", 0, 2, true)]),
            new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase) { ["a"] = "drop" });
        Check(host.LatestBotSteps.Single(s => s.Actor.Id == "a").Actor.Y == 1 && host.LatestBotSteps.Single(s => s.Actor.Id == "A").Actor.Y == 0,
            "XP eligibility belongs only to the exact bot ID");
        host.Encounter.RegisterEnemy("e", 1);
        await host.PublishSnapshotAsync(new(2, [new("e", 4, 0, true)]));
        Check(host.LatestBotSteps.Single(s => s.Actor.Id == "a").AttackTarget is null && host.LatestBotSteps.Single(s => s.Actor.Id == "A").AttackTarget == "e",
            "short and long ranges in the same snapshot");
    }
}
