using System.Text.Json.Serialization;

namespace Wsm.Core;

internal sealed record HeartbeatMessage(string Type, Guid Nonce);

[JsonSourceGenerationOptions(GenerationMode = JsonSourceGenerationMode.Metadata)]
[JsonSerializable(typeof(Message))]
[JsonSerializable(typeof(HeartbeatMessage))]
internal partial class WireJsonContext : JsonSerializerContext;
