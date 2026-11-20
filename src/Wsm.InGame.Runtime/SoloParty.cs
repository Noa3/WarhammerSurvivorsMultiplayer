using System.Text.Json.Serialization;
using Wsm.Core;

namespace Wsm.InGame.Runtime;

[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
internal sealed record HeroHealth([property: JsonRequired] string Id, [property: JsonRequired] float Hp,
    [property: JsonRequired] float MaxHp)
{
    internal static bool InRange(float hp, float max) => float.IsFinite(max) && max > 0 && hp >= 0 && hp <= max;
}
[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
internal sealed record SoloBot([property: JsonRequired] ObservedEntity Actor, [property: JsonRequired] float Hp,
    [property: JsonRequired] float MaxHp, [property: JsonRequired] float WeaponRange, [property: JsonRequired] float MaxStep,
    ObservedEntity? PersonalPickup = null, ObservedOffer? Offer = null);
[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
internal sealed record PartyWave([property: JsonRequired] long Id, [property: JsonRequired] string EnemyType);
[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
internal sealed record SoloPartyFrame([property: JsonRequired] long Run, [property: JsonRequired] long Tick,
    [property: JsonRequired] string Phase, [property: JsonRequired] HeroHealth Human, [property: JsonRequired] SoloBot[] Bots,
    [property: JsonRequired] ObservedEntity[] Enemies, PartyWave? Wave = null)
{
    internal ObservedBotFrame Frame(int i, string[] team) => new(Run, i, Tick, Human.Id, Phase, Bots[i].Actor,
        Bots[i].WeaponRange, Bots[i].MaxStep, Enemies, Bots[i].PersonalPickup, Bots[i].Offer, team);

    internal bool Valid(int count)
    {
        if (Human is null || !ObservedBotFrame.ValidId(Human.Id) || !HeroHealth.InRange(Human.Hp, Human.MaxHp) ||
            Bots is null || Bots.Length != count || count == 0 || Enemies is not { Length: <= 256 } ||
            (Wave is not null && (Wave.Id < 1 || !ObservedBotFrame.ValidId(Wave.EnemyType)))) return false;
        var team = new string[count];
        for (int i = 0; i < count; i++)
        {
            var b = Bots[i];
            if (b?.Actor is null || !HeroHealth.InRange(b.Hp, b.MaxHp) || b.Actor.Alive != (b.Hp > 0)) return false;
            team[i] = b.Actor.Id;
        }
        // All allies must already be identified, even for the first decision of a new run.
        for (int i = 0; i < count; i++) if (!Frame(i, team).Valid(count)) return false;
        return true;
    }
}
internal sealed record PartyBotDecision(string Actor, float Hp, float MaxHp, float X, float Y, string? AttackTarget,
    int? Level, string? Choice);
// Run/wave/slot is the stable key for one request in this host epoch. Anchor is a spawn
// region reference, not an exclusive target. Engine-side placement must choose a legal point.
internal sealed record ExtraSpawn(long Run, long Wave, int Slot, string Anchor, string EnemyType);
internal sealed record SoloPartyDecision(long Run, long Tick, bool RunEnded, HeroHealth Human,
    PartyBotDecision[] Bots, ExtraSpawn[] Spawns);

internal sealed partial class ObservedBotRoster
{
    private bool partyMode, partyEnded;
    private long partyTick;
    private PartyWave? lastObservedWave;
    private long lastProcessedWaveId;

    internal SoloPartyDecision PlanParty(SoloPartyFrame f, BotDifficulty difficulty)
    {
        bool fresh = f.Run > run;
        var ids = f.Bots.Select(b => b.Actor.Id).ToArray();
        if (!fresh && (f.Run < run || !partyMode || f.Tick <= partyTick || f.Human.Id != primary || !ids.SequenceEqual(team!)))
            throw new InvalidOperationException("Stale or conflicting solo party.");
        var observed = fresh ? null : lastObservedWave;
        long processedId = fresh ? 0 : lastProcessedWaveId;
        if (observed is not null && f.Wave is { } w && (w.Id < observed.Id || (w.Id == observed.Id && w != observed)))
            throw new InvalidOperationException("Stale or conflicting wave.");

        // Only the human determines the outcome. Even First Death does not turn a bot loss into a solo defeat.
        var ended = f.Human.Hp == 0 || (!fresh && partyEnded);
        // A pause or upgrade can establish a newer wave without consuming its one batch of orders.
        bool newWave = !ended && f.Phase == "playing" && f.Wave is not null && f.Wave.Id > processedId;
        var orders = new List<ExtraSpawn>();
        var decisions = new PartyBotDecision[ids.Length];
        for (int i = 0; i < ids.Length; i++)
        {
            var b = f.Bots[i];
            var d = ended ? null : Decide(f.Frame(i, ids), difficulty);
            decisions[i] = new(ids[i], b.Hp, b.MaxHp, d?.X ?? 0, d?.Y ?? 0, d?.AttackTarget, d?.Level, d?.Choice);
            if (newWave && b.Hp > 0) orders.Add(new(f.Run, f.Wave!.Id, i, ids[i], f.Wave.EnemyType));
        }
        var result = new SoloPartyDecision(f.Run, f.Tick, ended, f.Human, decisions, orders.ToArray());
        // Commit after every decision succeeds, so failed input can be corrected at the same tick.
        run = f.Run; primary = f.Human.Id; team = ids; partyMode = true;
        partyTick = f.Tick; partyEnded = ended;
        lastObservedWave = f.Wave ?? observed;
        lastProcessedWaveId = newWave ? f.Wave!.Id : processedId;
        return result;
    }
}
