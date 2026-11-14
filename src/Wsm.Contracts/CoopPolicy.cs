using System.Text.Json.Serialization;

namespace Wsm.Contracts;

public enum ExperienceSharing { Personal, Team }
public enum ExperienceDropLayout { Synchronized, Personal }
public enum PickupSharing { Personal, Team }
public enum DisconnectContinuation { WaitForReconnect, ProtectedContinue }

/// <summary>Immutable run rules. A game adapter must implement them before enabling gameplay.</summary>
public sealed record CoopPolicy
{
    [JsonRequired]
    public ExperienceSharing ExperienceSharing { get; init; } = ExperienceSharing.Personal;
    [JsonRequired]
    public ExperienceDropLayout ExperienceDropLayout { get; init; } = ExperienceDropLayout.Synchronized;
    [JsonRequired]
    public PickupSharing PickupSharing { get; init; } = PickupSharing.Personal;
    [JsonRequired]
    public int RecoveryPauseSeconds { get; init; } = 30;
    [JsonRequired]
    public DisconnectContinuation DisconnectContinuation { get; init; } = DisconnectContinuation.WaitForReconnect;
    [JsonRequired]
    public bool EnemyPositionsSynchronized { get; init; } = true;
    [JsonRequired]
    public bool EnemyHealthSynchronized { get; init; } = true;
    [JsonRequired]
    public bool IndependentLevelChoices { get; init; } = true;

    public void Validate()
    {
        if (!Enum.IsDefined(ExperienceSharing) || !Enum.IsDefined(ExperienceDropLayout) ||
            !Enum.IsDefined(PickupSharing) || !Enum.IsDefined(DisconnectContinuation) ||
            RecoveryPauseSeconds is < 0 or > 120 ||
            !EnemyPositionsSynchronized || !EnemyHealthSynchronized || !IndependentLevelChoices)
            throw new ArgumentException("Invalid co-op policy.");
        if (ExperienceSharing == ExperienceSharing.Team && ExperienceDropLayout != ExperienceDropLayout.Synchronized)
            throw new ArgumentException("Team XP requires synchronized, once-consumed drop events.");
    }
}
