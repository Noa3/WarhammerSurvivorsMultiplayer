namespace Wsm.Core;

// The host's trusted adapter supplies validated hits; this type does not validate game physics.
public sealed class Encounter
{
    private readonly object gate = new();
    private readonly Dictionary<string, EnemyHealth> enemies = new(StringComparer.Ordinal);
    private readonly HashSet<Guid> hits = [];

    public void RegisterEnemy(string id, int maxHp)
    {
        if (string.IsNullOrWhiteSpace(id) || id.Length > 128 || maxHp <= 0) throw new ArgumentException("Invalid enemy.");
        lock (gate)
        {
            if (enemies.Count >= 2048) throw new InvalidOperationException("Enemy capacity reached.");
            if (!enemies.TryAdd(id, new(id, maxHp, maxHp))) throw new ArgumentException("Duplicate enemy ID.");
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
