namespace Wsm.Core;

// The host's trusted adapter supplies validated hits; this type does not validate game physics.
public sealed class Encounter
{
    private readonly object gate = new();
    private readonly Dictionary<string, EnemyHealth> enemies = new(StringComparer.Ordinal);
    private readonly HashSet<Guid> hits = [];
    // Reserve for the largest tick and an empty entity array; HP never exceeds MaxHp.
    private int snapshotBytes = Peer.Encode(new Message("snapshot", Snapshot: new WorldSnapshot(long.MaxValue, []))).Length;

    public void RegisterEnemy(string id, int maxHp)
    {
        if (string.IsNullOrWhiteSpace(id) || id.Length > 128 || maxHp <= 0) throw new ArgumentException("Invalid enemy.");
        lock (gate)
        {
            if (enemies.Count >= 2048) throw new InvalidOperationException("Enemy capacity reached.");
            if (enemies.ContainsKey(id)) throw new ArgumentException("Duplicate enemy ID.");
            var enemy = new EnemyHealth(id, maxHp, maxHp);
            var bytes = System.Text.Json.JsonSerializer.SerializeToUtf8Bytes(enemy).Length + (enemies.Count == 0 ? 0 : 1);
            if (snapshotBytes + bytes > Peer.MaxFrame) throw new InvalidOperationException("Enemy snapshot frame capacity reached.");
            enemies.Add(id, enemy);
            snapshotBytes += bytes;
        }
    }

    // True exactly when this hit causes the alive-to-dead transition.
    public bool ApplyValidatedHit(Guid hitId, string enemyId, int damage)
    {
        if (hitId == Guid.Empty || string.IsNullOrWhiteSpace(enemyId) || damage <= 0) throw new ArgumentException("Invalid hit.");
        lock (gate)
        {
            if (!enemies.TryGetValue(enemyId, out var enemy)) throw new ArgumentException("Unknown enemy.");
            if (hits.Contains(hitId)) return false;
            if (hits.Count >= 100_000) throw new InvalidOperationException("Hit receipt capacity reached.");
            hits.Add(hitId);
            if (enemy.Hp == 0) return false;
            var hp = (int)Math.Max(0L, (long)enemy.Hp - damage);
            enemies[enemyId] = enemy with { Hp = hp };
            return hp == 0;
        }
    }

    public EnemyHealth[] Snapshot() { lock (gate) return enemies.Values.ToArray(); }
}
