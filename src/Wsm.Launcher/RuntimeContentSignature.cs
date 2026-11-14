using System.Security.Cryptography;
using System.Text.Json;

namespace Wsm.Launcher;

internal static class RuntimeContentSignature
{
    private sealed record FileContent(long Bytes, string Sha256);
    public static string Capture(string root, CancellationToken cancellationToken)
    {
        var files = new SortedDictionary<string, FileContent>(StringComparer.Ordinal);
        foreach (var path in RuntimePreparation.EnumerateIncludedFiles(root, cancellationToken))
        {
            cancellationToken.ThrowIfCancellationRequested();
            if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
                throw new IOException("Refusing a linked runtime file: " + path);
            using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
            files.Add(Path.GetRelativePath(root, path).Replace('\\', '/'), new(stream.Length, GameInspection.HashStream(stream, cancellationToken)));
        }
        cancellationToken.ThrowIfCancellationRequested();
        return Convert.ToHexStringLower(SHA256.HashData(JsonSerializer.SerializeToUtf8Bytes(files)));
    }
}
