using System.Buffers.Binary;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;

namespace Wsm.Launcher;

/// <summary>Read-only, exact-build inputs retained until the startup debugger detaches.</summary>
internal sealed class StartupUpgradeProfile : IDisposable
{
    internal const uint MethodRva = 0x49A7400;
    internal const int MethodSize = 1365;
    internal const uint PatchRva = 0x49A773D;
    internal const int PatchOffset = checked((int)(PatchRva - MethodRva));
    internal const int ImageSize = 118001664;
    private const string AssemblyHash = "840e1f68d5e8110d05ab0aff9087c36da75ccb5998566dccc17bc96605f94791";
    private const string MetadataHash = "f7b632c69bd5ea2dcbb41e96e4f78689e41d23ba37e1edf8780db2e8909e7ace";
    private const string OriginalHash = "029a955dc59cfc301a70fa4fa05643b77967d8172784621c9801e2e35e8bd7f2";
    private const string PatchedHash = "86c4a1e8ec549d1939355674bd045867dfc4b356b7589f8ad42a577db8d06767";
    private readonly FileStream assembly;
    private readonly FileStream metadata;
    private readonly byte[] original;
    private readonly byte[] patched;
    private readonly int ntOffset;
    private readonly int optionalSize;
    internal string AssemblyPath { get; }
    internal int HeaderReadSize { get; }
    internal byte[] OriginalMethod => (byte[])original.Clone();
    internal byte[] PatchedMethod => (byte[])patched.Clone();

    private StartupUpgradeProfile(string path, FileStream assembly, FileStream metadata,
        byte[] original, byte[] patched, int ntOffset, int optionalSize)
    {
        AssemblyPath = path;
        this.assembly = assembly;
        this.metadata = metadata;
        this.original = original;
        this.patched = patched;
        this.ntOffset = ntOffset;
        this.optionalSize = optionalSize;
        HeaderReadSize = Math.Max(4096, checked(ntOffset + 24 + optionalSize));
    }

    internal static StartupUpgradeProfile Create(string directory, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        ArgumentException.ThrowIfNullOrWhiteSpace(directory);
        var root = Path.GetFullPath(directory);
        var path = Path.Combine(root, "GameAssembly.dll");
        var metadataPath = Path.Combine(root, "WarhammerSurvivors_Data", "il2cpp_data", "Metadata", "global-metadata.dat");
        FileStream? assembly = null, metadata = null;
        try
        {
            assembly = OpenRegular(path);
            metadata = OpenRegular(metadataPath);
            VerifyFile(assembly, 115045376, AssemblyHash, "GameAssembly", cancellationToken);
            VerifyFile(metadata, 27884160, MetadataHash, "metadata", cancellationToken);
            assembly.Position = 0;
            var dos = new byte[64];
            assembly.ReadExactly(dos);
            var ntOffset = BinaryPrimitives.ReadInt32LittleEndian(dos.AsSpan(60, 4));
            assembly.Position = 0;
            using var pe = new PEReader(assembly, PEStreamOptions.LeaveOpen);
            var headers = pe.PEHeaders;
            var image = headers.PEHeader;
            if (headers.IsCoffOnly || image == null || image.Magic != PEMagic.PE32Plus ||
                headers.CoffHeader.Machine != Machine.Amd64 || headers.CorHeader != null ||
                (headers.CoffHeader.Characteristics & (Characteristics.ExecutableImage | Characteristics.Dll)) !=
                    (Characteristics.ExecutableImage | Characteristics.Dll) || image.SizeOfImage != ImageSize ||
                ntOffset < 64 || ntOffset > 4096 || headers.CoffHeader.SizeOfOptionalHeader < 112 ||
                checked(ntOffset + 24 + headers.CoffHeader.SizeOfOptionalHeader) > 8192)
                throw Unsupported("PE image");
            var section = headers.SectionHeaders.SingleOrDefault(s =>
                MethodRva >= s.VirtualAddress && (ulong)MethodRva + MethodSize <= (ulong)s.VirtualAddress + (uint)s.SizeOfRawData);
            if (((uint)section.SectionCharacteristics & 0x60000000u) != 0x60000000u)
                throw Unsupported("upgrade method section");
            var original = pe.GetSectionData(checked((int)MethodRva)).GetContent(0, MethodSize).ToArray();
            if (original.Length != MethodSize || Digest(original) != OriginalHash || original[PatchOffset] != 0xed)
                throw Unsupported("upgrade method fingerprint");
            var patched = (byte[])original.Clone();
            patched[PatchOffset] = 0x25;
            if (Digest(patched) != PatchedHash) throw Unsupported("redirected method fingerprint");
            cancellationToken.ThrowIfCancellationRequested();
            return new StartupUpgradeProfile(path, assembly, metadata, original, patched,
                ntOffset, headers.CoffHeader.SizeOfOptionalHeader);
        }
        catch
        {
            metadata?.Dispose();
            assembly?.Dispose();
            throw;
        }
    }

    internal void ValidateMappedHeaders(ReadOnlySpan<byte> bytes)
    {
        if (bytes.Length < HeaderReadSize || BinaryPrimitives.ReadUInt16LittleEndian(bytes) != 0x5a4d ||
            BinaryPrimitives.ReadInt32LittleEndian(bytes.Slice(60, 4)) != ntOffset ||
            BinaryPrimitives.ReadUInt32LittleEndian(bytes.Slice(ntOffset, 4)) != 0x4550 ||
            BinaryPrimitives.ReadUInt16LittleEndian(bytes.Slice(ntOffset + 4, 2)) != 0x8664 ||
            BinaryPrimitives.ReadUInt16LittleEndian(bytes.Slice(ntOffset + 20, 2)) != optionalSize ||
            (BinaryPrimitives.ReadUInt16LittleEndian(bytes.Slice(ntOffset + 22, 2)) & 0x2002) != 0x2002 ||
            BinaryPrimitives.ReadUInt16LittleEndian(bytes.Slice(ntOffset + 24, 2)) != 0x20b ||
            BinaryPrimitives.ReadUInt32LittleEndian(bytes.Slice(ntOffset + 80, 4)) != ImageSize)
            throw Unsupported("mapped PE identity");
    }

    private static FileStream OpenRegular(string path)
    {
        for (var parent = new DirectoryInfo(Path.GetDirectoryName(path)!); parent != null; parent = parent.Parent)
            if ((parent.Attributes & FileAttributes.ReparsePoint) != 0)
                throw new IOException("Linked upgrade-guard paths are not supported.");
        if ((File.GetAttributes(path) & (FileAttributes.ReparsePoint | FileAttributes.Directory)) != 0)
            throw new IOException("Upgrade-guard input must be a regular file.");
        return new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read, 128 * 1024, FileOptions.SequentialScan);
    }

    private static void VerifyFile(FileStream stream, long length, string digest, string name, CancellationToken cancellationToken)
    {
        if (stream.Length != length) throw Unsupported(name + " file size");
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        var buffer = new byte[128 * 1024];
        while (true)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var count = stream.Read(buffer);
            cancellationToken.ThrowIfCancellationRequested();
            if (count == 0) break;
            hash.AppendData(buffer, 0, count);
        }
        if (stream.Position != length || Convert.ToHexStringLower(hash.GetHashAndReset()) != digest)
            throw Unsupported(name + " SHA256");
    }

    private static string Digest(byte[] bytes) => Convert.ToHexStringLower(SHA256.HashData(bytes));
    private static InvalidDataException Unsupported(string reason) => new("Unsupported upgrade-guard " + reason + ".");

    public void Dispose()
    {
        metadata.Dispose();
        assembly.Dispose();
    }
}
