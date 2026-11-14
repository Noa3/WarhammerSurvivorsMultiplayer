using System.Security.Cryptography;
using Wsm.Launcher;

internal static class CacheIntegrationRegression
{
    // This optional integration test uses our original Unity player, never the installed game.
    public static void Run(string root, string fixture)
    {
        var source = Path.Combine(root, "original-unity-cache-source");
        Directory.CreateDirectory(source);
        foreach (var name in new[] { "GameAssembly.dll", "UnityPlayer.dll" })
            File.Copy(Path.Combine(fixture, name), Path.Combine(source, name));
        File.Copy(Path.Combine(fixture, "Fixture.exe"), Path.Combine(source, "WarhammerSurvivors.exe"));
        CopyDirectory(Path.Combine(fixture, "Fixture_Data"), Path.Combine(source, "WarhammerSurvivors_Data"));
        var sourceAsset = Path.Combine(source, "WarhammerSurvivors_Data", "wsm-cache-regression.txt");
        File.WriteAllText(sourceAsset, "version one");
        var original = Fingerprint(source);
        var cache = Path.Combine(root, "integration-cache");
        var prepared = OwnedGameLaunch.PrepareRuntimeAsync(source, cache).GetAwaiter().GetResult();
        var inspection = GameInspection.Inspect(prepared);
        if (inspection.MetadataVersion != 31) throw new Exception("Original Unity fixture metadata did not reach the supported path.");
        var firstFiles = Fingerprint(prepared);
        var reused = OwnedGameLaunch.PrepareRuntimeAsync(source, cache).GetAwaiter().GetResult();
        if (reused != prepared || !Equal(firstFiles, Fingerprint(prepared))) throw new Exception("Cache reuse changed prepared files.");
        var cachedAsset = Path.Combine(prepared, "WarhammerSurvivors_Data", "wsm-cache-regression.txt");
        foreach (var remove in new[] { false, true })
        {
            if (remove) File.Delete(cachedAsset); else File.WriteAllText(cachedAsset, "changed asset");
            bool assetRejected = false;
            try { OwnedGameLaunch.PrepareRuntimeAsync(source, cache).GetAwaiter().GetResult(); }
            catch (Exception ex) when (ex is InvalidDataException or IOException) { assetRejected = true; }
            if (!assetRejected) throw new Exception("Changed or removed cached asset was accepted.");
            File.WriteAllText(cachedAsset, "version one");
        }
        File.WriteAllText(sourceAsset, "version two");
        var updated = OwnedGameLaunch.PrepareRuntimeAsync(source, cache).GetAwaiter().GetResult();
        if (updated == prepared || File.ReadAllText(Path.Combine(updated, "WarhammerSurvivors_Data", "wsm-cache-regression.txt")) != "version two")
            throw new Exception("An asset-only update reused stale runtime data.");
        File.WriteAllText(sourceAsset, "version one");
        File.AppendAllText(Path.Combine(prepared, "GameAssembly.dll"), "tampered");
        bool rejected = false;
        try { OwnedGameLaunch.PrepareRuntimeAsync(source, cache).GetAwaiter().GetResult(); }
        catch (InvalidDataException) { rejected = true; }
        if (!rejected) throw new Exception("A changed copied runtime was accepted.");
        if (!Equal(original, Fingerprint(source))) throw new Exception("Runtime cache operations modified the original fixture.");
        if (Directory.EnumerateDirectories(cache, ".wsm-*").Any()) throw new Exception("Preparation leaked staging files.");
        Console.WriteLine("Original Unity fixture cache validated, reused byte-identically, tampering refused; no player process started.");
    }
    private static Dictionary<string, string> Fingerprint(string root) => Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories)
        .ToDictionary(path => Path.GetRelativePath(root, path), path => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))));
    private static bool Equal(Dictionary<string, string> a, Dictionary<string, string> b) => a.Count == b.Count && a.All(pair => b.TryGetValue(pair.Key, out var value) && value == pair.Value);
    private static void CopyDirectory(string source, string target)
    {
        Directory.CreateDirectory(target);
        foreach (var file in Directory.EnumerateFiles(source)) File.Copy(file, Path.Combine(target, Path.GetFileName(file)));
        foreach (var directory in Directory.EnumerateDirectories(source)) CopyDirectory(directory, Path.Combine(target, Path.GetFileName(directory)));
    }
}
