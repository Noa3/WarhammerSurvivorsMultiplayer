using System.Text;

namespace Wsm.Core;

/// <summary>Untrusted presentation only; IDs are assigned by the session host.</summary>
public sealed record PlayerName(Guid PlayerId, string Name);

public static class DisplayNames
{
    public const int MaxScalars = 48;
    public const int MaxUtf8Bytes = 192;

    public static string Sanitize(string? value)
    {
        var result = new StringBuilder();
        var count = 0;
        if (value is not null)
        {
            var remaining = value.AsSpan();
            while (!remaining.IsEmpty && count < MaxScalars)
            {
                var status = Rune.DecodeFromUtf16(remaining, out var rune, out var consumed);
                remaining = remaining[consumed..];
                if (status != System.Buffers.OperationStatus.Done || Rune.IsControl(rune) || rune.Value is 0x2028 or 0x2029) continue;
                result.Append(rune.ToString());
                count++;
            }
        }
        var name = result.ToString().Trim();
        return string.IsNullOrWhiteSpace(name) ? "Player" : name;
    }

    internal static bool Valid(string? name) => name is not null && name == Sanitize(name) && Encoding.UTF8.GetByteCount(name) <= MaxUtf8Bytes;

    internal static bool ValidRoster(PlayerName[]? roster, int maxClients, Guid localId) =>
        roster is { Length: > 0 } && roster.Length <= maxClients + 1 &&
        roster.All(p => p is not null && p.PlayerId != Guid.Empty && Valid(p.Name)) &&
        roster.Select(p => p.PlayerId).Distinct().Count() == roster.Length &&
        roster.Any(p => p.PlayerId == localId);
}
