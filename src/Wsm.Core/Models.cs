namespace Wsm.Core;

public enum DeathMode { AllDead, AnyDeathEndsRun }
public enum BotDifficulty { Easy, Normal, Hard }
public sealed record SessionSettings(int MaxClients, DeathMode DeathMode, int BotCount, BotDifficulty BotDifficulty)
{
    internal void Validate()
    {
        if (MaxClients is < 1 or > 8 || BotCount is < 0 or > 8 || !Enum.IsDefined(DeathMode) || !Enum.IsDefined(BotDifficulty))
            throw new ArgumentException("Invalid session settings.");
    }
}
public sealed record PlayerInput(Guid PlayerId, float X, float Y)
{
    internal bool Valid => PlayerId != Guid.Empty && float.IsFinite(X) && float.IsFinite(Y) && X is >= -1 and <= 1 && Y is >= -1 and <= 1 && X * X + Y * Y <= 1.00001f;
}
public sealed record UpgradeChoice(Guid PlayerId, int Level, string Choice);
public sealed record SessionCommand(Guid PlayerId, PlayerInput? Input, UpgradeChoice? Upgrade);
public sealed record WorldEntity(string Id, float X, float Y, bool Alive);
public sealed record EnemyHealth(string Id, int MaxHp, int Hp);
public sealed record WorldSnapshot(long Tick, WorldEntity[] Entities)
{
    public EnemyHealth[] Enemies { get; init; } = [];
    internal bool Valid => Tick >= 0 && Entities is { Length: <= 2048 } && Entities.All(e => e is not null && !string.IsNullOrWhiteSpace(e.Id) && e.Id.Length <= 128 && float.IsFinite(e.X) && float.IsFinite(e.Y)) && Enemies is { Length: <= 2048 } && Enemies.All(e => e is not null && !string.IsNullOrWhiteSpace(e.Id) && e.Id.Length <= 128 && e.MaxHp > 0 && e.Hp >= 0 && e.Hp <= e.MaxHp) && Enemies.Select(e => e.Id).Distinct(StringComparer.Ordinal).Count() == Enemies.Length;
}
public enum RewardKind { Xp, Item, Artifact }
public sealed record RewardReceipt(Guid ReceiptId, Guid RecipientId, RewardKind Kind, int Amount, string? ItemId = null)
{
    internal bool Valid => ReceiptId != Guid.Empty && RecipientId != Guid.Empty && Enum.IsDefined(Kind) && Amount > 0 && (Kind == RewardKind.Xp ? ItemId is null : !string.IsNullOrWhiteSpace(ItemId) && ItemId.Length <= 128);
}

public sealed class SessionState(DeathMode mode)
{
    private readonly Dictionary<Guid, bool> players = [];
    public bool RunEnded { get; private set; }
    public void AddPlayer(Guid id) { if (id == Guid.Empty || players.Count >= 16 || !players.TryAdd(id, true)) throw new ArgumentException("Invalid, duplicate, or excess player."); }
    public void RemovePlayer(Guid id) { players.Remove(id); Evaluate(); }
    public void MarkDead(Guid id)
    {
        if (!players.ContainsKey(id)) throw new ArgumentException("Unknown player.");
        players[id] = false;
        if (mode == DeathMode.AnyDeathEndsRun) RunEnded = true;
        Evaluate();
    }
    public bool IsSpectating(Guid id) => players.TryGetValue(id, out var alive) && !alive && !RunEnded;
    private void Evaluate() { if (players.Count > 0 && players.Values.All(alive => !alive)) RunEnded = true; }
}

public static class BotPolicy
{
    public static PlayerInput Move(Guid botId, long tick, BotDifficulty difficulty)
    {
        if (botId == Guid.Empty || tick < 0 || !Enum.IsDefined(difficulty)) throw new ArgumentException("Invalid bot input.");
        var speed = difficulty switch { BotDifficulty.Easy => 0.4f, BotDifficulty.Normal => 0.7f, _ => 1f };
        var bytes = botId.ToByteArray();
        var direction = (int)((tick / 30 + bytes[0]) % 4);
        return direction switch { 0 => new(botId, speed, 0), 1 => new(botId, 0, speed), 2 => new(botId, -speed, 0), _ => new(botId, 0, -speed) };
    }
}
