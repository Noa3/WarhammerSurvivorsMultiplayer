using System.Text.Json.Serialization;
using Wsm.Core;

namespace Wsm.InGame.Runtime;

// Observations come from the local host adapter, not from the peer protocol. Require every
// position component: a missing coordinate must not be interpreted as a real position at 0.
[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
internal sealed record ObservedEntity([property: JsonRequired] string Id, [property: JsonRequired] float X,
    [property: JsonRequired] float Y, [property: JsonRequired] bool Alive)
{
    internal bool Valid => ObservedBotFrame.ValidId(Id) && float.IsFinite(X) && float.IsFinite(Y);
    internal WorldEntity ToWorld() => new(Id, X, Y, Alive);
}
[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
internal sealed record ObservedOffer([property: JsonRequired] string Owner, [property: JsonRequired] int Level,
    [property: JsonRequired] string[] Choices);
[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
internal sealed record ObservedBotFrame([property: JsonRequired] long Run, [property: JsonRequired] int Slot,
    [property: JsonRequired] long Tick, [property: JsonRequired] string Primary, [property: JsonRequired] string Phase,
    [property: JsonRequired] ObservedEntity Actor, [property: JsonRequired] float WeaponRange,
    [property: JsonRequired] float MaxStep, [property: JsonRequired] ObservedEntity[] Enemies,
    ObservedEntity? PersonalPickup = null, ObservedOffer? Offer = null, string[]? Team = null)
{
    internal static bool ValidId(string? id) => !string.IsNullOrWhiteSpace(id) && id.Length <= 128;
    internal bool Valid(int count)
    {
        if (Run <= 0 || Tick < 0 || Slot < 0 || Slot >= count || !ValidId(Primary) || Actor is not { Valid: true } ||
            Actor.Id == Primary || Phase is not ("playing" or "paused" or "upgrade") || !float.IsFinite(WeaponRange) ||
            WeaponRange <= 0 || !float.IsFinite(MaxStep) || MaxStep <= 0 || Enemies is not { Length: <= 256 }) return false;
        var ids = new HashSet<string>(StringComparer.Ordinal) { Primary };
        if (Team is null)
        {
            if (count != 1) return false;
            ids.Add(Actor.Id);
        }
        else if (Team.Length != count || Team[Slot] != Actor.Id || Team.Any(id => !ValidId(id) || !ids.Add(id))) return false;
        if (PersonalPickup is not null && (!PersonalPickup.Valid || !ids.Add(PersonalPickup.Id))) return false;
        if (Enemies.Any(e => e is not { Valid: true } || !ids.Add(e.Id))) return false;
        if (Offer is null) return true; // No selectable offer yet: remain neutral in the modal.
        return Phase == "upgrade" && ValidId(Offer.Owner) && Offer.Level is >= 1 and <= 10000 &&
            Offer.Choices is { Length: > 0 and <= 32 } && Offer.Choices.All(ValidId) &&
            Offer.Choices.Distinct(StringComparer.Ordinal).Count() == Offer.Choices.Length;
    }
}
internal sealed record ObservedBotDecision(long Run, int Slot, long Tick, string Actor, float X, float Y,
    string? AttackTarget, int? Level, string? Choice);

// One instance per host session; only its single outgoing reader calls Plan. A decision is
// an intent, never an engine position, damage/XP receipt or accepted equipment change.
internal sealed partial class ObservedBotRoster
{
    private sealed record Binding(string Actor, long Tick);
    private readonly Binding?[] bindings = new Binding?[8];
    private long run;
    private string? primary;
    private string[]? team;

    internal ObservedBotDecision Plan(ObservedBotFrame f, SessionSettings settings)
    {
        // Frame shape and host capacity have already been validated at admission.
        if (f.Run < run) throw new InvalidOperationException("Expired bot run.");
        if (f.Run == run)
        {
            if (partyMode || (f.Team is not null && !team!.SequenceEqual(f.Team)))
                throw new InvalidOperationException("Different party or observation mode within run.");
            if (primary != f.Primary) throw new InvalidOperationException("Primary actor changed within run.");
            var bound = bindings[f.Slot];
            if (bound is not null && (bound.Actor != f.Actor.Id || f.Tick <= bound.Tick))
                throw new InvalidOperationException("Stale tick or different actor for slot.");
            for (int i = 0; i < bindings.Length; i++)
                if (i != f.Slot && bindings[i] is { } other &&
                    (other.Actor == f.Actor.Id || f.Enemies.Any(e => e.Id == other.Actor) || f.PersonalPickup?.Id == other.Actor))
                    throw new InvalidOperationException("Conflicting bot actor roles.");
        }

        var decision = Decide(f, settings.BotDifficulty);
        if (f.Run != run)
        {
            Array.Clear(bindings); run = f.Run; primary = f.Primary; team = f.Team ?? [f.Actor.Id]; partyMode = false;
        }
        bindings[f.Slot] = new Binding(f.Actor.Id, f.Tick);
        return decision;
    }

    private static ObservedBotDecision Decide(ObservedBotFrame f, BotDifficulty difficulty)
    {
        float x = 0, y = 0;
        string? target = null, choice = null;
        int? level = null;
        if (f.Actor.Alive && f.Phase == "playing")
        {
            var cmd = SessionHost.PlanObservedBot(f.Actor.ToWorld(), f.WeaponRange, f.MaxStep,
                difficulty, f.Tick, f.Enemies.Select(e => e.ToWorld()).ToArray(), f.PersonalPickup?.ToWorld());
            (x, y, target) = (cmd.X, cmd.Y, cmd.AttackTarget);
        }
        else if (f.Actor.Alive && f.Phase == "upgrade" && f.Offer is { } offer && offer.Owner == f.Actor.Id)
        {
            // No guessed equipment IDs or costs. Vary by slot and level but keep the result
            // deterministic if UI rows are rebuilt in a different order on the next frame.
            var choices = offer.Choices.Order(StringComparer.Ordinal).ToArray();
            level = offer.Level;
            choice = choices[(f.Slot + offer.Level - 1) % choices.Length];
        }


        return new(f.Run, f.Slot, f.Tick, f.Actor.Id, x, y, target, level, choice);
    }
}
