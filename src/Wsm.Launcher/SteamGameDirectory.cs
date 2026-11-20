using System.Runtime.Versioning;
using System.Security;
using System.Text.RegularExpressions;
using Microsoft.Win32;

namespace Wsm.Launcher;

internal static class SteamGameDirectory
{
    [SupportedOSPlatform("windows")]
    internal static IEnumerable<string?> SteamRoots()
    {
        if (Environment.GetEnvironmentVariable("WSM_STEAM_ROOT") is { Length: > 0 } configured)
        {
            yield return configured;
            yield break;
        }
        yield return RegistryPath(@"HKEY_CURRENT_USER\Software\Valve\Steam", "SteamPath");
        yield return RegistryPath(@"HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath");
        yield return RegistryPath(@"HKEY_LOCAL_MACHINE\SOFTWARE\Valve\Steam", "InstallPath");
        var programs = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86);
        if (programs.Length != 0) yield return Path.Combine(programs, "Steam");
    }

    [SupportedOSPlatform("windows")]
    private static string? RegistryPath(string key, string value)
    {
        try { return Registry.GetValue(key, value, null) as string; }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or SecurityException) { return null; }
    }

    internal static string? Select(string? explicitPath, IEnumerable<string?> roots, TextReader input, TextWriter output)
    {
        if (!string.IsNullOrEmpty(explicitPath)) return RequireGame(explicitPath);
        var found = Find(roots);
        if (found != null) output.WriteLine("Steam game folder: " + found);
        output.Write(found == null ? "Game not found in Steam. Paste your game folder (blank to cancel): " :
            "Press Enter to use this folder, or paste a different game folder: ");
        var answer = input.ReadLine();
        if (answer == null) return null;
        return string.IsNullOrWhiteSpace(answer) ? found : RequireGame(answer);
    }

    private static string RequireGame(string path)
    {
        path = path.Trim();
        if (path.Length >= 2 && path[0] == '"' && path[^1] == '"') path = path[1..^1];
        if (!IsLocalPath(path) || !LooksLikeGame(path))
            throw new ArgumentException("Not an installed Warhammer Survivors game folder: " + path);
        return Path.GetFullPath(path);
    }

    private static bool IsLocalPath(string path) => Path.IsPathFullyQualified(path) &&
        (!OperatingSystem.IsWindows() || (char.IsAsciiLetter(path[0]) && path[1] == ':')) &&
        path.IndexOfAny(Path.GetInvalidPathChars()) < 0;

    // Discovery checks layout only; play still performs the full native image/metadata checks.
    private static bool LooksLikeGame(string folder) => File.Exists(Path.Combine(folder, "WarhammerSurvivors.exe")) &&
        File.Exists(Path.Combine(folder, "UnityPlayer.dll")) && File.Exists(Path.Combine(folder, "GameAssembly.dll")) &&
        File.Exists(Path.Combine(folder, "WarhammerSurvivors_Data", "il2cpp_data", "Metadata", "global-metadata.dat"));

    internal static string? Find(IEnumerable<string?> roots)
    {
        var visited = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var root in roots)
        {
            if (root == null || !IsLocalPath(root)) continue;
            foreach (var library in Libraries(root))
            {
                if (!IsLocalPath(library) || !visited.Add(library)) continue;
                try
                {
                    var common = Path.Combine(library, "steamapps", "common");
                    foreach (var folder in Directory.EnumerateDirectories(common))
                        if (LooksLikeGame(folder)) return Path.GetFullPath(folder);
                }
                catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { /* Stale or unavailable library. */ }
            }
        }
        return null;
    }

    private static IEnumerable<string> Libraries(string root)
    {
        yield return root;
        string[] lines;
        try { lines = File.ReadAllLines(Path.Combine(root, "steamapps", "libraryfolders.vdf")); }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { yield break; }
        foreach (var line in lines)
        {
            // Steam writes one quoted pair per line: modern "path" or legacy numeric index.
            var match = Regex.Match(line, """^\s*"(?:path|[0-9]+)"\s+"([^"]+)"\s*(?://.*)?$""", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
            if (match.Success) yield return match.Groups[1].Value.Replace(@"\\", @"\");
        }
    }
}
