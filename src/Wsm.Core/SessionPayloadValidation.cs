namespace Wsm.Core;

// Shared admission rules for the native adapter and the TCP boundary. Methods
// (rather than DTO properties) keep the existing serialized wire shape intact.
public static class SessionPayloadValidation
{
    public static bool IsValid(PlayerInput? input) => input is { Valid: true };
    public static bool IsValid(WorldSnapshot? snapshot) => snapshot is { Valid: true };
    public static bool IsValid(RewardReceipt? reward) => reward is { Valid: true };
    public static bool IsValid(UpgradeChoice? choice) => choice is not null && choice.PlayerId != Guid.Empty &&
        choice.Level is > 0 and <= 10000 && !string.IsNullOrWhiteSpace(choice.Choice) && choice.Choice.Length <= 128;
}
