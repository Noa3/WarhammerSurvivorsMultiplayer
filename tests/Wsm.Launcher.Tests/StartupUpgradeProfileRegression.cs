using System.Security.Cryptography;
using Wsm.Launcher;

internal static class StartupUpgradeProfileRegression
{
    internal static void Run(string root)
    {
        var directory = Path.Combine(root, "upgrade-profile");
        Directory.CreateDirectory(directory);
        using (var cancel = new CancellationTokenSource())
        {
            cancel.Cancel();
            Expect<OperationCanceledException>(() => StartupUpgradeProfile.Create(Path.Combine(directory, "missing"), cancel.Token));
        }
        Console.WriteLine("PASS startup profile cancellation precedes filesystem access");
        Expect<FileNotFoundException>(() => StartupUpgradeProfile.Create(directory, CancellationToken.None));
        if (Directory.EnumerateFileSystemEntries(directory).Any()) throw new Exception("Missing profile created files");
        Console.WriteLine("PASS startup profile missing files refuse without writes");
        var assembly = Path.Combine(directory, "GameAssembly.dll");
        var metadata = Path.Combine(directory, "WarhammerSurvivors_Data", "il2cpp_data", "Metadata", "global-metadata.dat");
        Directory.CreateDirectory(Path.GetDirectoryName(metadata)!);
        File.WriteAllBytes(assembly, [0x4d, 0x5a, 0, 0]);
        File.WriteAllBytes(metadata, [0xaf, 0x1b, 0xb1, 0xfa, 31, 0, 0, 0]);
        var beforeAssembly = SHA256.HashData(File.ReadAllBytes(assembly));
        var beforeMetadata = SHA256.HashData(File.ReadAllBytes(metadata));
        var error = Expect<InvalidDataException>(() => StartupUpgradeProfile.Create(directory, CancellationToken.None));
        if (!error.Message.Contains("Unsupported upgrade-guard", StringComparison.Ordinal)) throw new Exception("Wrong profile rejection: " + error.Message);
        Console.WriteLine("PASS startup profile wrong version refuses explicitly");
        if (!beforeAssembly.SequenceEqual(SHA256.HashData(File.ReadAllBytes(assembly))) ||
            !beforeMetadata.SequenceEqual(SHA256.HashData(File.ReadAllBytes(metadata)))) throw new Exception("Profile inputs changed");
        if (Directory.EnumerateFiles(directory, "*", SearchOption.AllDirectories).Count() != 2) throw new Exception("Unexpected profile files");
        Console.WriteLine("PASS startup profile refused inputs remain byte-identical");
        using (var file = new FileStream(assembly, FileMode.Open, FileAccess.Write)) file.SetLength(115045376);
        using (var file = new FileStream(metadata, FileMode.Open, FileAccess.Write)) file.SetLength(27884160);
        byte[] assemblyDigest;
        using (var file = File.OpenRead(assembly)) assemblyDigest = SHA256.HashData(file);
        error = Expect<InvalidDataException>(() => StartupUpgradeProfile.Create(directory, CancellationToken.None));
        if (error.Message != "Unsupported upgrade-guard GameAssembly SHA256.") throw new Exception("SHA guard not reached: " + error.Message);
        Console.WriteLine("PASS startup profile correct-size wrong-hash refuses at SHA guard");
        using (var file = File.OpenRead(assembly))
            if (!assemblyDigest.SequenceEqual(SHA256.HashData(file))) throw new Exception("SHA refusal changed input");
        Console.WriteLine("PASS startup profile SHA refusal preserves source bytes");
    }

    private static T Expect<T>(Action action) where T : Exception
    {
        try { action(); }
        catch (T error) { return error; }
        throw new Exception("Expected " + typeof(T).Name);
    }
}
