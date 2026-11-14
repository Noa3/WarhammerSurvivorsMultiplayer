using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

namespace Wsm.Launcher;

public static class RuntimePreparation
{
    public const string MarkerName = ".wsm-runtime";
    private static readonly string[] RequiredFiles = ["WarhammerSurvivors.exe", "UnityPlayer.dll", "GameAssembly.dll"];
    private static readonly string[] OptionalFiles = ["baselib.dll", "UnityCrashHandler64.exe"];

    public static void Prepare(string gameDirectory, string destination)
    {
        var source = Path.TrimEndingDirectorySeparator(Path.GetFullPath(gameDirectory));
        var target = Path.TrimEndingDirectorySeparator(Path.GetFullPath(destination));
        RejectReparseAncestors(source);
        RejectReparseAncestors(target);
        var canonicalSource = CanonicalPath(source);
        var canonicalTarget = CanonicalPath(target);
        if (IsWithin(canonicalTarget, canonicalSource) || IsWithin(canonicalSource, canonicalTarget))
            throw new ArgumentException("The runtime directory must be separate from the game installation.");
        if (Directory.Exists(target) || File.Exists(target))
            throw new IOException("The destination already exists; choose a new empty runtime path. Nothing was overwritten.");

        foreach (var file in RequiredFiles)
            if (!File.Exists(Path.Combine(source, file)))
                throw new FileNotFoundException("Required game file is missing: " + file);
        var data = Path.Combine(source, "WarhammerSurvivors_Data");
        if (!Directory.Exists(data))
            throw new DirectoryNotFoundException("WarhammerSurvivors_Data is missing.");

        var parent = Path.GetDirectoryName(target)!;
        Directory.CreateDirectory(parent);
        var staging = Path.Combine(parent, ".wsm-staging-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(staging);
        Exception? primary = null;
        try
        {
            foreach (var file in RequiredFiles.Concat(OptionalFiles))
            {
                var path = Path.Combine(source, file);
                if (File.Exists(path)) CopyFile(path, Path.Combine(staging, file));
            }
            CopyDirectory(data, Path.Combine(staging, "WarhammerSurvivors_Data"));
            var d3d = Path.Combine(source, "D3D12");
            if (Directory.Exists(d3d)) CopyDirectory(d3d, Path.Combine(staging, "D3D12"));
            File.WriteAllText(Path.Combine(staging, MarkerName), "WSM isolated runtime v1\n" + source + "\n");
            Directory.Move(staging, target);
        }
        catch (Exception error) { primary = error; throw; }
        finally
        {
            FailureCleanup.Run(() => { if (Directory.Exists(staging)) Directory.Delete(staging, true); }, primary);
        }
    }

    internal static bool IsWithin(string path, string parent) =>
        path.Equals(parent, StringComparison.OrdinalIgnoreCase) ||
        path.StartsWith(Path.EndsInDirectorySeparator(parent) ? parent : parent + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);

    internal static string CanonicalPath(string path)
    {
        path = Path.TrimEndingDirectorySeparator(Path.GetFullPath(path));
        if (!OperatingSystem.IsWindows()) return path;
        var ancestor = path;
        var missing = new Stack<string>();
        while (!Directory.Exists(ancestor) && !File.Exists(ancestor))
        {
            var parent = Path.GetDirectoryName(ancestor);
            if (parent == null) throw new IOException("Cannot resolve an existing path ancestor: " + path);
            missing.Push(Path.GetFileName(ancestor));
            ancestor = parent;
        }
        var buffer = new StringBuilder(32768);
        var length = GetLongPathNameW(ancestor, buffer, (uint)buffer.Capacity);
        if (length == 0) throw new IOException("Cannot resolve filesystem path: " + ancestor, new Win32Exception(Marshal.GetLastWin32Error()));
        if (length >= buffer.Capacity) throw new IOException("Resolved filesystem path is too long: " + ancestor);
        var result = buffer.ToString();
        foreach (var part in missing) result = Path.Combine(result, part);
        return Path.TrimEndingDirectorySeparator(result);
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern uint GetLongPathNameW(string path, StringBuilder result, uint capacity);

    internal static void RejectReparseAncestors(string path)
    {
        for (var current = new DirectoryInfo(path); current != null; current = current.Parent)
            if (current.Exists && (current.Attributes & FileAttributes.ReparsePoint) != 0)
                throw new IOException("Symbolic links and junctions are not allowed in runtime paths: " + current.FullName);
    }

    private static void CopyFile(string source, string target)
    {
        if ((File.GetAttributes(source) & FileAttributes.ReparsePoint) != 0)
            throw new IOException("Refusing a linked game file: " + source);
        // Real copies deliberately avoid writes through hardlinks into the original installation.
        File.Copy(source, target, false);
        File.SetAttributes(target, File.GetAttributes(target) & ~FileAttributes.ReadOnly);
    }

    private static void CopyDirectory(string source, string target)
    {
        if ((File.GetAttributes(source) & FileAttributes.ReparsePoint) != 0)
            throw new IOException("Refusing a linked game directory: " + source);
        Directory.CreateDirectory(target);
        foreach (var file in Directory.EnumerateFiles(source))
            CopyFile(file, Path.Combine(target, Path.GetFileName(file)));
        foreach (var directory in Directory.EnumerateDirectories(source))
        {
            if (Path.GetFileName(directory).Equals("AsanaReportCache", StringComparison.OrdinalIgnoreCase)) continue;
            CopyDirectory(directory, Path.Combine(target, Path.GetFileName(directory)));
        }
    }
}
