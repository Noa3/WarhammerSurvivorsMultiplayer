using Wsm.Launcher;

internal static class SteamDirectoryRegression
{
    internal static void Libraries(string root)
    {
        var steam = Path.Combine(root, "Steam root");
        var apps = Directory.CreateDirectory(Path.Combine(steam, "steamapps")).FullName;
        var vdf = Path.Combine(apps, "libraryfolders.vdf");
        var library = Path.Combine(root, "Spiele & Spaß [alt]! (2) %PATH% ^");
        var alternate = MakeGame(Path.Combine(library, "steamapps", "common", "Warhammer Survivors"));
        var escaped = library.Replace(@"\", @"\\");
        foreach (var entry in new[] { $"\"1\"\n{{\n\t\"path\"\t\"{escaped}\"\n}}", $"\"1\"\t\"{escaped}\"" })
        {
            File.WriteAllText(vdf, "\"libraryfolders\"\n{\n\"0\" \"relative\"\n" + entry + "\n}\n");
            Equal(alternate, SteamGameDirectory.Find([null, Path.Combine(root, "stale"), steam]));
        }
        var primary = Path.Combine(apps, "common", "Warhammer Survivors");
        Directory.CreateDirectory(primary);
        File.WriteAllText(Path.Combine(primary, "WarhammerSurvivors.exe"), "incomplete");
        Equal(alternate, SteamGameDirectory.Find([steam]));
        MakeGame(primary);
        File.Delete(vdf);
        Equal(primary, SteamGameDirectory.Find([steam]));
    }

    internal static void Selection(string root)
    {
        var steam = Path.Combine(root, "selection-steam");
        var found = MakeGame(Path.Combine(steam, "steamapps", "common", "Game"));
        var manual = MakeGame(Path.Combine(root, "manual & ! %PATH% ^ (1)"));
        var missing = Path.Combine(root, "no-steam");
        string? Pick(string? configured, string location, string answer) =>
            SteamGameDirectory.Select(configured, [location], new StringReader(answer), TextWriter.Null);
        Equal(found, Pick(null, steam, "\n"));
        Equal(manual, Pick(null, steam, $"\"{manual}\"\n"));
        Equal(manual, Pick(null, missing, manual));
        Equal(manual, Pick(manual, steam, ""));
        Equal(null, Pick(null, missing, "\n"));
        Equal(null, Pick(null, steam, ""));
        foreach (var invalid in new[] { missing, "relative", manual + "\" & exit 0" })
        {
            try { Pick(null, steam, invalid); }
            catch (ArgumentException) { continue; }
            throw new Exception("Invalid override accepted: " + invalid);
        }
    }

    private static string MakeGame(string folder)
    {
        var metadata = Directory.CreateDirectory(Path.Combine(folder, "WarhammerSurvivors_Data", "il2cpp_data", "Metadata"));
        File.WriteAllText(Path.Combine(metadata.FullName, "global-metadata.dat"), "fixture");
        foreach (var file in new[] { "WarhammerSurvivors.exe", "UnityPlayer.dll", "GameAssembly.dll" })
            File.WriteAllText(Path.Combine(folder, file), "not executable");
        return folder;
    }

    private static void Equal(string? expected, string? actual)
    {
        if (expected != actual) throw new Exception($"Expected '{expected}', got '{actual}'.");
    }
}
