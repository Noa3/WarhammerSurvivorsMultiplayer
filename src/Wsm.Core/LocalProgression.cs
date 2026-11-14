namespace Wsm.Core;

public sealed class LocalProgression(Guid playerId)
{
    private readonly HashSet<Guid> receipts = [];
    private readonly Dictionary<int, string> choices = [];
    public Guid PlayerId { get; } = playerId != Guid.Empty ? playerId : throw new ArgumentException("Player ID is required.", nameof(playerId));
    private readonly object gate = new();
    private long xp;
    public long Xp { get { lock (gate) return xp; } }
    public IReadOnlyDictionary<int, string> Choices { get { lock (gate) return new System.Collections.ObjectModel.ReadOnlyDictionary<int, string>(new Dictionary<int, string>(choices)); } }
    // Receipts are bounded rather than evicted, because eviction would permit replay.
    public bool ApplyXp(Guid receiptId, Guid recipient, int amount)
    {
        if (recipient != PlayerId || receiptId == Guid.Empty || amount <= 0) throw new ArgumentException("Invalid XP receipt.");
        lock (gate)
        {
            if (receipts.Contains(receiptId)) return false;
            if (receipts.Count >= 100_000) throw new InvalidOperationException("XP receipt capacity reached.");
            var next = checked(xp + amount);
            receipts.Add(receiptId);
            xp = next;
            return true;
        }
    }
    public void ChooseUpgrade(int level, string choice)
    {
        lock (gate)
            if (level is <= 0 or > 10000 || string.IsNullOrWhiteSpace(choice) || choice.Length > 128 || !choices.TryAdd(level, choice)) throw new ArgumentException("Invalid or duplicate level choice.");
    }
}
