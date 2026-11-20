namespace Wsm.Core;

// Host-local results only. A target is an attack intent, never proof of a hit.
public sealed record BotStep(long Tick, WorldEntity Actor, string? AttackTarget);
public readonly record struct BotCommand(float X, float Y, string? AttackTarget);

public sealed partial class SessionHost
{
    private sealed record SimulatedBot(WorldEntity Actor, float WeaponRange, float MaxStep);
    private readonly Dictionary<string, SimulatedBot> bots = new(StringComparer.Ordinal);
    private BotStep[] botSteps = [];
    public BotStep[] LatestBotSteps { get { lock (snapshotLock) return [.. botSteps]; } }

    // Opt-in Core actors, not connected players. The adapter supplies distances in snapshot units.
    public void RegisterSimulatedBot(WorldEntity spawn, float weaponRange, float maxStep)
    {
        ArgumentNullException.ThrowIfNull(spawn);
        if (!spawn.Alive || string.IsNullOrWhiteSpace(spawn.Id) || spawn.Id.Length > 128 ||
            !float.IsFinite(spawn.X) || !float.IsFinite(spawn.Y) || !float.IsFinite(weaponRange) ||
            weaponRange <= 0 || !float.IsFinite(maxStep) || maxStep <= 0) throw new ArgumentException("Invalid bot spawn or distances.");
        lock (snapshotLock)
        {
            if (bots.Count >= settings.BotCount) throw new InvalidOperationException("Configured bot capacity reached.");
            bots.Add(spawn.Id, new(spawn, weaponRange, maxStep));
        }
    }

    public bool RemoveSimulatedBot(string id) { lock (snapshotLock) return bots.Remove(id); }

    // Optional, host-only hint for this publication: bot ID -> one currently collectible XP
    // entity ID. The adapter owns eligibility; a hint never collects XP or publishes a reward.
    public Task PublishSnapshotWithBotGoalsAsync(WorldSnapshot snapshot, IReadOnlyDictionary<string, string> xpByBot, CancellationToken token = default)
        => PublishSnapshotCoreAsync(snapshot, xpByBot ?? throw new ArgumentNullException(nameof(xpByBot)), token);

    // Called under snapshotLock. Plan against exactly the HP cut that will be sent. Don't move
    // the authoritative actors until validation, encoding and cancellation checks have passed.
    private BotStep[] PlanBots(WorldSnapshot world, IReadOnlyDictionary<string, string>? xpByBot)
    {
        if (xpByBot?.Count > bots.Count) throw new ArgumentException("Too many bot goals.");
        if (bots.Count == 0) return [];
        if (world.Entities.Length + bots.Count > 2048) throw new ArgumentException("Bots exceed snapshot entity capacity.");
        var goals = new Dictionary<string, WorldEntity?>(StringComparer.Ordinal);
        // Snapshot exact bot identities; never inherit the adapter dictionary's comparer.
        var eligible = xpByBot is null ? null : new Dictionary<string, string>(xpByBot, StringComparer.Ordinal);
        if (eligible is not null)
            foreach (var (id, goal) in eligible)
            {
                if (!bots.ContainsKey(id) || string.IsNullOrWhiteSpace(goal) || goal.Length > 128 || bots.ContainsKey(goal))
                    throw new ArgumentException("Invalid bot or XP goal ID.");
                goals.TryAdd(goal, null);
            }
        var alive = new HashSet<string>(StringComparer.Ordinal);
        foreach (var enemy in world.Enemies)
        {
            if (bots.ContainsKey(enemy.Id)) throw new ArgumentException("Bot ID collides with registered enemy.");
            if (goals.ContainsKey(enemy.Id)) throw new ArgumentException("Enemy cannot be an XP goal.");
            if (enemy.Hp > 0) alive.Add(enemy.Id);
        }
        var threats = new List<WorldEntity>();
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (var entity in world.Entities)
        {
            if (bots.ContainsKey(entity.Id)) throw new ArgumentException("Caller supplied a simulated bot position.");
            if (!alive.Contains(entity.Id) && !goals.ContainsKey(entity.Id)) continue;
            if (!seen.Add(entity.Id)) throw new ArgumentException("Ambiguous bot observation.");
            if (!entity.Alive) continue;
            if (goals.ContainsKey(entity.Id)) goals[entity.Id] = entity;
            else threats.Add(entity);
        }
        return bots.Values.Select(bot =>
        {
            // A known live enemy without a position cannot be cleared from a pickup route.
            var xp = threats.Count == alive.Count && eligible is not null && eligible.TryGetValue(bot.Actor.Id, out var id) ? goals[id] : null;
            return PlanBot(bot, world.Tick, threats, xp);
        }).ToArray();
    }

    // The caller classifies enemies and pickup eligibility for one observation. Nothing is committed.
    public static BotCommand PlanObservedBot(WorldEntity actor, float weaponRange, float maxStep, BotDifficulty difficulty,
        long tick, IReadOnlyList<WorldEntity> enemies, WorldEntity? personalPickup)
    {
        ArgumentNullException.ThrowIfNull(enemies);
        int count = enemies.Count;
        if (count is < 0 or > 2048 || tick < 0 || !Enum.IsDefined(difficulty) ||
            !float.IsFinite(weaponRange) || weaponRange <= 0 || !float.IsFinite(maxStep) || maxStep <= 0)
            throw new ArgumentException("Invalid observation count, tick, difficulty or distances.");

        var ids = new HashSet<string>(StringComparer.Ordinal);
        void Validate(WorldEntity entity)
        {
            ArgumentNullException.ThrowIfNull(entity);
            if (string.IsNullOrWhiteSpace(entity.Id) || entity.Id.Length > 128 || !ids.Add(entity.Id) ||
                !float.IsFinite(entity.X) || !float.IsFinite(entity.Y)) throw new ArgumentException("Invalid or ambiguous observed entity.");
        }
        Validate(actor);
        if (personalPickup is not null) Validate(personalPickup);
        var threats = new List<WorldEntity>(count);
        for (int i = 0; i < count; i++)
        {
            var enemy = enemies[i];
            Validate(enemy);
            if (enemy.Alive) threats.Add(enemy);
        }
        if (!actor.Alive) return default;

        var (dx, dy) = PlanMovement(actor, weaponRange, maxStep, difficulty, threats,
            personalPickup is { Alive: true } ? personalPickup : null, out var attackTarget);
        if (OutsideWorld(actor.X, dx) || OutsideWorld(actor.Y, dy)) throw new ArgumentException("Planned step exceeds coordinate bounds.");
        float x = (float)(dx / maxStep), y = (float)(dy / maxStep);
        // Rounding a unit direction to two floats can put the result just outside the disk.
        double major = Math.Max(Math.Abs(x), Math.Abs(y)), minor = Math.Min(Math.Abs(x), Math.Abs(y));
        if (minor * minor > (1 - major) * (1 + major))
        {
            if (Math.Abs(x) >= Math.Abs(y)) x = MathF.CopySign(MathF.BitDecrement(Math.Abs(x)), x);
            else y = MathF.CopySign(MathF.BitDecrement(Math.Abs(y)), y);
        }
        return new(x, y, attackTarget);

        static bool OutsideWorld(float start, double delta) =>
            delta > (double)float.MaxValue - start || delta < -(double)float.MaxValue - start;
    }

    private BotStep PlanBot(SimulatedBot bot, long tick, List<WorldEntity> threats, WorldEntity? xp)
    {
        var actor = bot.Actor;
        var (dx, dy) = PlanMovement(actor, bot.WeaponRange, bot.MaxStep, settings.BotDifficulty, threats, xp, out var target);
        if (dx != 0 || dy != 0)
        {
            actor = actor with { X = Coordinate(actor.X + dx), Y = Coordinate(actor.Y + dy) };
            var nearest = Nearest(actor, threats, out var distance);
            target = distance <= bot.WeaponRange ? nearest?.Id : null;
        }
        return new(tick, actor, target);
    }

    private static (double X, double Y) PlanMovement(WorldEntity actor, float weaponRange, float maxStep, BotDifficulty difficulty,
        List<WorldEntity> threats, WorldEntity? xp, out string? attackTarget)
    {
        var nearest = Nearest(actor, threats, out double distance);
        attackTarget = distance <= weaponRange ? nearest?.Id : null;

        var (speed, buffer) = difficulty switch
        {
            BotDifficulty.Easy => (0.4, 0.30),
            BotDifficulty.Normal => (0.7, 0.50),
            _ => (1.0, 0.70)
        };
        // Stop at the band edge, even when the configured stride exceeds the weapon range.
        double clearance = weaponRange * buffer;
        double stride = maxStep * speed;
        if (xp is not null && distance >= clearance)
        {
            double dx = (double)xp.X - actor.X, dy = (double)xp.Y - actor.Y;
            double length = Math.Sqrt(dx * dx + dy * dy);
            if (RouteClear(actor, dx, dy, threats, clearance)) return Move(actor, xp, length, Math.Min(length, stride));
        }
        if (nearest is null) return default;
        double desired = Math.Clamp(distance, clearance, weaponRange);
        var move = Move(actor, nearest, distance, Math.Clamp(distance - desired, -stride, stride));
        return distance < clearance && threats.Count > 1 ? SafeRetreat(actor, move, threats, distance) : move;
    }

    private static (double X, double Y) Move(WorldEntity actor, WorldEntity target, double distance, double travel)
    {
        if (travel == 0) return default;
        double dx = distance == 0 ? 1 : ((double)target.X - actor.X) / distance;
        double dy = distance == 0 ? 0 : ((double)target.Y - actor.Y) / distance;
        return (dx * travel, dy * travel);
    }

    private static bool RouteClear(WorldEntity actor, double dx, double dy, List<WorldEntity> threats, double clearance)
    {
        double lengthSquared = dx * dx + dy * dy;
        foreach (var enemy in threats)
        {
            // Closest point on the entire route, not just the pickup or the next step.
            double ex = (double)enemy.X - actor.X, ey = (double)enemy.Y - actor.Y;
            double t = lengthSquared == 0 ? 0 : Math.Clamp((ex * dx + ey * dy) / lengthSquared, 0, 1);
            double x = ex - t * dx, y = ey - t * dy;
            if (x * x + y * y < clearance * clearance) return false;
        }
        return true;
    }

    private static float Coordinate(double value) => (float)Math.Clamp(value, -float.MaxValue, float.MaxValue);

    private static (double, double) SafeRetreat(WorldEntity start, (double X, double Y) away, List<WorldEntity> threats, double distance)
    {
        (double, double) best = default;
        double bestDistance = distance;
        Consider(away);
        Consider((-away.Y, away.X));
        Consider((away.Y, -away.X));
        return best;

        void Consider((double X, double Y) candidate)
        {
            Nearest(start, threats, out var atEnd, candidate.X, candidate.Y);
            // Allow the starting distance itself despite sqrt roundoff. Never cross a closer enemy.
            if (atEnd <= bestDistance || !RouteClear(start, candidate.X, candidate.Y, threats, Math.Max(0, Math.BitDecrement(distance)))) return;
            best = candidate;
            bestDistance = atEnd;
        }
    }

    private static WorldEntity? Nearest(WorldEntity actor, List<WorldEntity> threats, out double distance, double offsetX = 0, double offsetY = 0)
    {
        WorldEntity? nearest = null;
        double best = double.PositiveInfinity;
        foreach (var enemy in threats)
        {
            double dx = ((double)enemy.X - actor.X) - offsetX, dy = ((double)enemy.Y - actor.Y) - offsetY;
            double squared = dx * dx + dy * dy;
            if (squared < best || (squared == best && string.CompareOrdinal(enemy.Id, nearest?.Id) < 0))
            {
                nearest = enemy;
                best = squared;
            }
        }
        distance = Math.Sqrt(best);
        return nearest;
    }

    private void CommitBotSteps(BotStep[] steps)
    {
        foreach (var step in steps) bots[step.Actor.Id] = bots[step.Actor.Id] with { Actor = step.Actor };
        botSteps = steps;
    }
}
