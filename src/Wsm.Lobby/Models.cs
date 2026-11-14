using System.Text.Json.Serialization;
using Wsm.Contracts;

namespace Wsm.Lobby;

public enum BotDifficulty { Easy, Normal, Hard }
public enum DefeatRule { AllPlayersDefeated, AnyPlayerDefeated }
public enum PausePolicy { GlobalUpgradesPersonalMenusNonPausingHostConfirmedManualPause }
public enum SlotKind { Host, Player, Bot }
public sealed record LobbyRules(int BotCount = 0, BotDifficulty BotDifficulty = BotDifficulty.Normal, DefeatRule DefeatRule = DefeatRule.AllPlayersDefeated)
{
    [JsonRequired]
    public CoopPolicy Policy { get; init; } = new();
    public PausePolicy PausePolicy => PausePolicy.GlobalUpgradesPersonalMenusNonPausingHostConfirmedManualPause;
    public void Validate()
    {
        if (BotCount is < 0 or > 3 || !Enum.IsDefined(BotDifficulty) || !Enum.IsDefined(DefeatRule))
            throw new ArgumentException("Invalid lobby rules.");
        ArgumentNullException.ThrowIfNull(Policy);
        Policy.Validate();
    }
}
public sealed record LobbySlot(Guid PlayerId, string Name, SlotKind Kind, string? HeroId, IReadOnlyList<string> WeaponIds, bool Ready);
public sealed record LobbySnapshot(long Version, LobbyRules Rules, IReadOnlyList<LobbySlot> Slots, bool Started);
public sealed record StartResult(bool Started, string Reason);
/// <summary>Implement only with a real game adapter. Heartbeats remain live while other lobby mutations are locked.</summary>
public interface IGameLaunchCoordinator { StartResult TryLaunch(LobbySnapshot snapshot); }
internal sealed record JoinRequest(string Name, int? ProtocolVersion = null);
internal sealed record JoinResponse(Guid PlayerId, string Token, int ProtocolVersion = 0);
internal sealed record LoadoutRequest(string HeroId, string[] WeaponIds);
internal sealed record ReadyRequest(bool? Ready);
internal sealed record ErrorResponse(string Error, string Code = "request_failed");
