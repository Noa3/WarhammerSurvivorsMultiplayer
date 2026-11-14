using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Runtime.Versioning;
using System.Text;

namespace Wsm.Launcher;

/// <summary>Loads the bridge only into a child created by this invocation; never attaches to an existing process.</summary>
[SupportedOSPlatform("windows")]
public sealed class ChildBootstrap : IDisposable
{
    public Process Process { get; }
    private bool disposed;

    private ChildBootstrap(Process process) => Process = process;

    public static ChildBootstrap Start(string executable, string bridge, string log, IEnumerable<string> arguments,
        string? gameLog = null, bool mainThreadProbe = false, bool enableLocalCoop = false, bool enableInGameUi = false)
    {
        if (!Environment.Is64BitProcess) throw new PlatformNotSupportedException("Use a Windows x64 .NET process.");
        executable = Path.GetFullPath(executable);
        bridge = Path.GetFullPath(bridge);
        log = Path.GetFullPath(log);
        GameInspection.RequireNativeX64(executable);
        GameInspection.RequireNativeX64(bridge);
        RuntimePreparation.RejectReparseAncestors(Path.GetDirectoryName(executable)!);
        RuntimePreparation.RejectReparseAncestors(Path.GetDirectoryName(bridge)!);
        Directory.CreateDirectory(Path.GetDirectoryName(log)!);
        if (File.Exists(log)) throw new IOException("Choose a fresh bridge log path to avoid mistaking an old result for success.");
        var info = new ProcessStartInfo(executable)
        {
            WorkingDirectory = Path.GetDirectoryName(executable)!,
            UseShellExecute = false
        };
        info.Environment["WSM_BRIDGE_LOG"] = log;
        if (gameLog != null) info.Environment["WSM_GAME_LOG"] = Path.GetFullPath(gameLog);
        else info.Environment.Remove("WSM_GAME_LOG");
        info.Environment["WSM_MAIN_THREAD_PROBE"] = mainThreadProbe ? "1" : "0";
        info.Environment["WSM_ENABLE_LOCAL_COOP"] = enableLocalCoop ? "1" : "0";
        ConfigureUiEnvironment(info, enableInGameUi);
        info.Environment["SteamAppId"] = "3669620";
        info.Environment["SteamGameId"] = "3669620";
        foreach (var argument in arguments) info.ArgumentList.Add(argument);
        var process = Process.Start(info) ?? throw new IOException("The game process did not start.");
        var child = new ChildBootstrap(process);
        try
        {
            child.LoadBridge(bridge, TimeSpan.FromSeconds(15));
            return child;
        }
        catch (Exception startError)
        {
            try { child.Dispose(); }
            catch (Exception cleanupError)
            {
                throw new IOException($"Start failed ({startError.Message}) and child cleanup failed ({cleanupError.Message}).",
                    new AggregateException(startError, cleanupError));
            }
            throw;
        }
    }

    internal static void ConfigureUiEnvironment(ProcessStartInfo info, bool enableInGameUi)
    {
        info.Environment["WSM_IN_GAME_UI"] = enableInGameUi ? "1" : "0";
        if (!enableInGameUi) return;
        foreach (var variable in new[] { "WSM_OBSERVATION_SIGNAL", "WSM_INGAME_STOP_SIGNAL", "WSM_INGAME_TEST_SECONDS", "WSM_PLAYER_OBSERVATION_DIR" })
            info.Environment.Remove(variable);
        info.Environment["WSM_MAIN_THREAD_PROBE"] = "0";
        info.Environment["WSM_ENABLE_LOCAL_COOP"] = "0";
    }

    private void LoadBridge(string bridge, TimeSpan timeout)
    {
        var bytes = Encoding.Unicode.GetBytes(bridge + '\0');
        var remote = Native.VirtualAllocEx(Process.Handle, 0, (nuint)bytes.Length, 0x3000, 0x04);
        if (remote == 0) throw Error("Allocate bridge path");
        nint thread = 0;
        var threadFinished = false;
        try
        {
            if (!Native.WriteProcessMemory(Process.Handle, remote, bytes, (nuint)bytes.Length, out var written) || written != (nuint)bytes.Length)
                throw Error("Write bridge path");
            var loader = RemoteLoadLibraryAddress();
            thread = Native.CreateRemoteThread(Process.Handle, 0, 0, loader, remote, 0, out _);
            if (thread == 0) throw Error("Start bridge loader in owned child");
            var result = Native.WaitForSingleObject(thread, (uint)timeout.TotalMilliseconds);
            if (result == 0xffffffff) throw Error("Wait for bridge loader");
            if (result != 0) throw new IOException("Bridge loading timed out; the owned child will be stopped.");
            threadFinished = true;
            Process.Refresh();
            var loaded = Process.Modules.Cast<ProcessModule>()
                .Any(module => string.Equals(Path.GetFullPath(module.FileName), bridge, StringComparison.OrdinalIgnoreCase));
            if (!loaded) throw new IOException("The child did not load the bridge. Check architecture and runtime DLL dependencies.");
        }
        finally
        {
            if (thread != 0) Native.CloseHandle(thread);
            // A timed-out thread may still use the path; disposing the child frees its address space safely.
            if (thread == 0 || threadFinished) Native.VirtualFreeEx(Process.Handle, remote, 0, 0x8000);
        }
    }

    private nint RemoteLoadLibraryAddress()
    {
        var kernel = Native.GetModuleHandleW("kernel32.dll");
        if (kernel == 0) throw Error("Find kernel32");
        var local = Native.GetProcAddress(kernel, "LoadLibraryW");
        if (local == 0) throw Error("Find LoadLibraryW");
        using var current = System.Diagnostics.Process.GetCurrentProcess();
        // Resolve forwarded exports by the module containing the actual address, not by assuming equal ASLR bases.
        var owner = current.Modules.Cast<ProcessModule>().Single(module =>
            local >= module.BaseAddress && local < module.BaseAddress + module.ModuleMemorySize);
        var offset = local - owner.BaseAddress;
        var timer = Stopwatch.StartNew();
        while (timer.Elapsed < TimeSpan.FromSeconds(5))
        {
            if (Process.HasExited) throw new IOException("The child exited before bridge loading.");
            Process.Refresh();
            var target = Process.Modules.Cast<ProcessModule>().FirstOrDefault(module =>
                string.Equals(module.ModuleName, owner.ModuleName, StringComparison.OrdinalIgnoreCase));
            if (target != null && offset >= 0 && offset < target.ModuleMemorySize)
                return target.BaseAddress + offset;
            Thread.Sleep(25);
        }
        throw new IOException("The child did not initialize the Windows loader in time.");
    }

    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        try
        {
            if (!Process.HasExited)
            {
                if (Process.CloseMainWindow() && Process.WaitForExit(3000)) return;
                Process.Kill(entireProcessTree: false);
                if (!Process.WaitForExit(5000)) throw new IOException("The owned child did not stop in time.");
            }
        }
        catch (InvalidOperationException) when (Process.HasExited) { }
        catch (Win32Exception) when (Process.HasExited) { }
        finally { Process.Dispose(); }
    }

    private static Win32Exception Error(string operation) => new(Marshal.GetLastWin32Error(), operation);

    private static class Native
    {
        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern nint VirtualAllocEx(nint process, nint address, nuint size, uint allocationType, uint protect);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool VirtualFreeEx(nint process, nint address, nuint size, uint freeType);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool WriteProcessMemory(nint process, nint address, byte[] buffer, nuint size, out nuint written);
        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern nint CreateRemoteThread(nint process, nint attributes, nuint stackSize, nint start, nint argument, uint flags, out uint id);
        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern uint WaitForSingleObject(nint handle, uint milliseconds);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool CloseHandle(nint handle);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        internal static extern nint GetModuleHandleW(string name);
        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true, SetLastError = true)]
        internal static extern nint GetProcAddress(nint module, string name);
    }
}
