using System.Diagnostics;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text.Json;

namespace Wsm.Launcher;

public sealed record GameInspection(string Directory, string ProductVersion, int MetadataVersion,
    IReadOnlyDictionary<string, string> Sha256, string[] ForeignLoaderFiles, bool HasVortexBackup)
{
    public static GameInspection Inspect(string directory)
    {
        var root = Path.GetFullPath(directory);
        RuntimePreparation.RejectReparseAncestors(root);
        var hashes = new SortedDictionary<string, string>(StringComparer.Ordinal);
        foreach (var file in new[] { "WarhammerSurvivors.exe", "UnityPlayer.dll", "GameAssembly.dll" })
        {
            var path = Path.Combine(root, file);
            RequireNativeX64(path);
            using var stream = File.OpenRead(path);
            hashes.Add(file, Convert.ToHexStringLower(SHA256.HashData(stream)));
        }
        var metadata = Path.Combine(root, "WarhammerSurvivors_Data", "il2cpp_data", "Metadata", "global-metadata.dat");
        using var reader = new BinaryReader(File.OpenRead(metadata));
        if (reader.ReadUInt32() != 0xfab11baf) throw new InvalidDataException("Not a recognized IL2CPP metadata file.");
        var version = reader.ReadInt32();
        if (version is < 24 or > 40) throw new InvalidDataException("Unsupported IL2CPP metadata version: " + version);
        reader.BaseStream.Position = 0;
        hashes.Add("global-metadata.dat", Convert.ToHexStringLower(SHA256.HashData(reader.BaseStream)));
        var conflicts = new[] { "version.dll", "winhttp.dll", "dinput8.dll", "doorstop_config.ini", "MelonLoader", "BepInEx" }
            .Where(file => File.Exists(Path.Combine(root, file)) || System.IO.Directory.Exists(Path.Combine(root, file))).ToArray();
        return new(root, FileVersionInfo.GetVersionInfo(Path.Combine(root, "WarhammerSurvivors.exe")).ProductVersion ?? "unknown",
            version, hashes, conflicts, File.Exists(Path.Combine(root, "GameAssembly.dll.vortex_backup")));
    }

    public static void RequireNativeX64(string path)
    {
        if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
            throw new IOException("Linked executables are not permitted: " + path);
        using var stream = File.OpenRead(path);
        using var pe = new PEReader(stream);
        var headers = pe.PEHeaders;
        var flags = headers.CoffHeader.Characteristics;
        var expectsDll = Path.GetExtension(path).Equals(".dll", StringComparison.OrdinalIgnoreCase);
        if (headers.IsCoffOnly || headers.PEHeader?.Magic != PEMagic.PE32Plus ||
            headers.CoffHeader.Machine != Machine.Amd64 || headers.CorHeader != null ||
            (flags & Characteristics.ExecutableImage) == 0 || expectsDll != ((flags & Characteristics.Dll) != 0))
            throw new InvalidDataException("Expected a native Windows x64 image: " + path);
    }

    public string ToJson() => JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true });
}
