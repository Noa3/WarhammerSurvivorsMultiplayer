using System.ComponentModel;
using System.Diagnostics;
using System.Reflection.PortableExecutable;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using System.Text;
using static Wsm.Launcher.StartupDebugNative;

namespace Wsm.Launcher;

/// <summary>Startup debugging of one newly-created child, never attachment to a PID.</summary>
internal static class StartupDebugProcess
{
    // Callbacks are trusted internal code and must return cooperatively. The deadline
    // bounds event waits and certificate operations, not arbitrary blocking user code.
    internal static Process Start(ProcessStartInfo start, string expectedImagePath, Action<StoppedImage> apply,
        CancellationToken cancellationToken, TimeSpan timeout)
    {
        cancellationToken.ThrowIfCancellationRequested();
        ArgumentNullException.ThrowIfNull(start);
        ArgumentNullException.ThrowIfNull(apply);
        if (!OperatingSystem.IsWindows() || !Environment.Is64BitProcess)
            throw new PlatformNotSupportedException("Windows x64 is required.");
        if (timeout <= TimeSpan.Zero || timeout > TimeSpan.FromMinutes(2)) throw new ArgumentOutOfRangeException(nameof(timeout));
        if (start.UseShellExecute || start.RedirectStandardInput || start.RedirectStandardOutput || start.RedirectStandardError ||
            start.Arguments.Length != 0 || !string.IsNullOrEmpty(start.UserName))
            throw new ArgumentException("Use an owned native child, ArgumentList, no shell, redirection or alternate identity.");
        string executable = Normalize(start.FileName), expected = Normalize(expectedImagePath);
        using var executableFile = OpenImage(executable, false);
        using var expectedFile = OpenImage(expected, true);
        var expectedIdentity = Identity(expectedFile.SafeFileHandle.DangerousGetHandle());
        string directory = Normalize(string.IsNullOrEmpty(start.WorkingDirectory) ? Path.GetDirectoryName(executable)! : start.WorkingDirectory);
        if (!Directory.Exists(directory)) throw new DirectoryNotFoundException(directory);
        string command = string.Join(" ", new[] { executable }.Concat(start.ArgumentList).Select(Quote));
        if (command.Length >= 32767) throw new ArgumentException("Child command line is too long.");
        string environment = EnvironmentBlock(start);
        Process? result = null;
        Exception? failure = null;
        var thread = new Thread(() =>
        {
            try { result = Run(executable, expected, expectedIdentity, command, environment, directory, start.CreateNoWindow, apply, cancellationToken, timeout); }
            catch (Exception error) { failure = error; }
        }) { Name = "WSM startup debugger", IsBackground = true };
        thread.Start();
        thread.Join();
        if (failure != null) ExceptionDispatchInfo.Capture(failure).Throw();
        return result ?? throw new IOException("Startup debugger returned no owned child.");
    }

    private static Process Run(string executable, string expected, FileInformation identity, string command, string environment,
        string directory, bool hidden, Action<StoppedImage> apply, CancellationToken cancellation, TimeSpan timeout)
    {
        PROCESS_INFORMATION child = default;
        DebugEventInfo current = default;
        bool outstanding = false, exited = false, loaderBreakpoint = false;
        Process? managed = null;
        nint environmentBuffer = Marshal.StringToHGlobalUni(environment);
        var clock = Stopwatch.StartNew();
        var threads = new Dictionary<uint, nint>();
        ulong ntdllBase = 0, ntdllEnd = 0;
        void CheckBudget()
        {
            cancellation.ThrowIfCancellationRequested();
            if (clock.Elapsed >= timeout) throw new TimeoutException("Startup image did not complete before its deadline.");
        }
        try
        {
            CheckBudget();
            var startup = new STARTUPINFOW { Size = (uint)Marshal.SizeOf<STARTUPINFOW>() };
            if (!CreateProcessW(executable, new StringBuilder(command), 0, 0, false,
                0x402u | (hidden ? 0x08000000u : 0), environmentBuffer, directory, ref startup, out child))
                throw Error("Create owned debug child");
            while (true)
            {
                CheckBudget();
                if (!WaitForDebugEvent(out current, 50))
                {
                    if (Marshal.GetLastWin32Error() == 121) continue;
                    throw Error("Wait for owned debug event");
                }
                if (current.ProcessId != child.ProcessId) throw new IOException("Unexpected debug-event process identity.");
                outstanding = true;
                uint continuation = DBGContinue;
                bool found = false;
                try
                {
                    switch (current.Code)
                    {
                        case CreateProcessEvent:
                            if (current.ThreadId != child.ThreadId || current.MainThreadHandle == 0 ||
                                !PathsEqual(FinalPath(current.FileHandle), executable))
                                throw new IOException("Created debug process identity mismatch.");
                            threads.Add(current.ThreadId, current.MainThreadHandle);
                            break;
                        case CreateThreadEvent:
                            if (current.ThreadHandle == 0 || !threads.TryAdd(current.ThreadId, current.ThreadHandle))
                                throw new IOException("Invalid or duplicate debug thread.");
                            break;
                        case ExitThreadEvent:
                            threads.Remove(current.ThreadId); // ContinueDebugEvent closes the debug handle.
                            break;
                        case ExitProcessEvent:
                            exited = true;
                            threads.Clear();
                            break;
                        case LoadDllEvent:
                        {
                            string? path = current.FileHandle == 0 ? null : FinalPath(current.FileHandle);
                            if (path != null && PathsEqual(path, Path.Combine(Environment.SystemDirectory, "ntdll.dll")))
                            {
                                ntdllBase = (ulong)current.DllBase;
                                ntdllEnd = checked(ntdllBase + ImageSize(child.Process, ntdllBase));
                            }
                            if (path != null && PathsEqual(path, expected))
                            {
                                var loaded = Identity(current.FileHandle);
                                if (loaded.Volume != identity.Volume || loaded.IndexHigh != identity.IndexHigh || loaded.IndexLow != identity.IndexLow)
                                    throw new IOException("Loaded image file identity differs from the locked expected file.");
                                var image = new StoppedImage(child.Process, child.ProcessId, (ulong)current.DllBase,
                                    ImageSize(child.Process, (ulong)current.DllBase), new Dictionary<uint, nint>(threads), CheckBudget);
                                try
                                {
                                    // Never continue this event before the callback and all checks finish.
                                    apply(image);
                                    image.CheckComplete();
                                }
                                finally { image.Expire(); }
                                managed = Process.GetProcessById((int)child.ProcessId);
                                _ = managed.Handle; // Pin identity before releasing the creation handle.
                                found = true;
                            }
                            break;
                        }
                        case ExceptionEvent:
                            if (!loaderBreakpoint && current.ThreadId == child.ThreadId && current.FirstChance == 1 &&
                                current.ExceptionCode == 0x80000003 && current.ExceptionFlags == 0 &&
                                (ulong)current.ExceptionAddress >= ntdllBase && (ulong)current.ExceptionAddress < ntdllEnd)
                                loaderBreakpoint = true;
                            else continuation = DBGExceptionNotHandled;
                            break;
                        case UnloadDllEvent:
                            if ((ulong)current.UnloadBase == ntdllBase) { ntdllBase = 0; ntdllEnd = 0; }
                            break;
                        case 8: break; // OUTPUT_DEBUG_STRING_EVENT, never read arbitrary remote strings.
                        default: throw new IOException("Unsupported startup debug event: " + current.Code);
                    }
                }
                finally { CloseEventFile(ref current); }
                CheckBudget();
                if (!ContinueDebugEvent(current.ProcessId, current.ThreadId, continuation)) throw Error("Continue owned debug event");
                outstanding = false;
                if (exited) throw new IOException("Owned child exited before the requested startup image was ready.");
                if (!found) continue;
                if (!DebugActiveProcessStop(child.ProcessId)) throw Error("Detach owned startup debugger");
                // The Win32 debug API releases its event-owned process/thread handles
                // on detach/continued exit. Only the separate creation handles are ours.
                threads.Clear();
                return managed!;
            }
        }
        catch (Exception error)
        {
            bool cleaned = child.Process == 0;
            Exception? cleanupError = null;
            try
            {
                if (child.Process != 0)
                {
                    if (!exited && !TerminateProcess(child.Process, 0xC0000AD4) && WaitForSingleObject(child.Process, 0) != 0)
                        throw Error("Terminate failed owned startup");
                    if (outstanding)
                    {
                        CloseEventFile(ref current);
                        if (!ContinueDebugEvent(current.ProcessId, current.ThreadId, DBGContinue)) throw Error("Continue terminated owned child");
                        outstanding = false;
                    }
                    var drain = Stopwatch.StartNew();
                    while (!exited && drain.Elapsed < TimeSpan.FromSeconds(5))
                    {
                        if (!WaitForDebugEvent(out var pending, 50))
                        {
                            if (Marshal.GetLastWin32Error() == 121) continue;
                            throw Error("Drain owned debug exit");
                        }
                        if (pending.ProcessId != child.ProcessId) throw new IOException("Foreign event during owned cleanup.");
                        CloseEventFile(ref pending);
                        if (!ContinueDebugEvent(pending.ProcessId, pending.ThreadId,
                            pending.Code == ExceptionEvent ? DBGExceptionNotHandled : DBGContinue)) throw Error("Continue owned cleanup event");
                        exited = pending.Code == ExitProcessEvent;
                    }
                    if (WaitForSingleObject(child.Process, 2000) != 0) throw new TimeoutException("Owned startup child cleanup was not confirmed.");
                    cleaned = true;
                }
            }
            catch (Exception cleanup) { cleanupError = cleanup; }
            managed?.Dispose();
            Exception failure = cleanupError == null ? error : new AggregateException("Startup and owned cleanup failed.", error, cleanupError);
            failure.Data["OwnedChildPid"] = child.ProcessId;
            failure.Data["OwnedChildExitConfirmed"] = cleaned;
            ExceptionDispatchInfo.Capture(failure).Throw();
            throw;
        }
        finally
        {
            if (child.Thread != 0) CloseHandle(child.Thread);
            if (child.Process != 0) CloseHandle(child.Process);
            Marshal.FreeHGlobal(environmentBuffer);
        }
    }

    internal sealed class StoppedImage
    {
        internal const int RspOffset = 152, RipOffset = 248;
        private readonly nint process;
        private readonly uint owner = GetCurrentThreadId();
        private readonly Dictionary<uint, nint> threads;
        private readonly Action checkBudget;
        private bool active = true, poisoned, written;
        internal ulong ImageBase { get; }
        internal uint ProcessId { get; }
        private readonly uint size;
        internal StoppedImage(nint process, uint pid, ulong imageBase, uint size, Dictionary<uint, nint> threads, Action budget)
        { this.process = process; ProcessId = pid; ImageBase = imageBase; this.size = size; this.threads = threads; checkBudget = budget; }
        private void Check()
        {
            if (!active || GetCurrentThreadId() != owner) throw new InvalidOperationException("Stopped-image certificate expired or used on a foreign thread.");
            checkBudget();
        }
        internal void Expire() => active = false;
        internal void CheckComplete() { Check(); if (poisoned) throw new IOException("An uncertain code write cannot be ignored."); }
        internal byte[] ReadImage(uint rva, int count)
        {
            Check();
            if (count <= 0 || count > 65536 || rva >= size || (ulong)rva + (uint)count > size)
                throw new ArgumentOutOfRangeException(nameof(rva), "Read is outside the stopped image.");
            return ReadMapped(process, checked(ImageBase + rva), count, ImageBase);
        }
        internal void ReplaceCodeByte(uint rva, int offset, byte[] originalMethod, byte[] patchedMethod)
        {
            Check();
            ArgumentNullException.ThrowIfNull(originalMethod);
            ArgumentNullException.ThrowIfNull(patchedMethod);
            if (written) throw new InvalidOperationException("Only one startup code write is permitted.");
            if (originalMethod.Length == 0 || originalMethod.Length > 16384 || originalMethod.Length != patchedMethod.Length)
                throw new ArgumentException("Invalid method lengths.");
            if (offset < 0 || offset >= originalMethod.Length) throw new ArgumentOutOfRangeException(nameof(offset));
            var original = (byte[])originalMethod.Clone(); var patched = (byte[])patchedMethod.Clone();
            if (Enumerable.Range(0, original.Length).Count(i => original[i] != patched[i]) != 1 || original[offset] == patched[offset])
                throw new ArgumentException("Exactly one byte must differ at the given offset.");
            if (!ReadImage(rva, original.Length).AsSpan().SequenceEqual(original)) throw new IOException("Loaded original method mismatch.");
            ulong method = checked(ImageBase + rva), address = checked(method + (uint)offset);
            VerifyQuiescence(method, (uint)original.Length);
            var before = Query(process, address);
            if ((before.Protect & 0xff) is not (0x20 or 0x40 or 0x80)) throw new IOException("Patch location is not readable executable memory.");
            poisoned = true;
            if (!VirtualProtectEx(process, (nuint)address, 1, PageExecuteReadWrite, out uint protection)) throw Error("Make own code byte writable");
            Exception? failure = null;
            try
            {
                Check();
                if (!WriteProcessMemory(process, (nuint)address, [patched[offset]], 1, out nuint changed) || changed != 1) throw Error("Write own code byte");
                if (!FlushInstructionCache(process, (nuint)address, 1)) throw Error("Flush own instruction cache");
            }
            catch (Exception error)
            {
                failure = error;
                try
                {
                    byte observed = ReadRaw(process, address, 1)[0];
                    if (observed == patched[offset])
                    {
                        if (!WriteProcessMemory(process, (nuint)address, [original[offset]], 1, out nuint restored) || restored != 1 ||
                            !FlushInstructionCache(process, (nuint)address, 1) || ReadRaw(process, address, 1)[0] != original[offset])
                            throw Error("Restore owned byte after failed patch");
                    }
                    else if (observed != original[offset]) throw new IOException("Foreign byte encountered; refusing rollback overwrite.");
                }
                catch (Exception rollback) { failure = new AggregateException(error, rollback); }
            }
            finally
            {
                if (!VirtualProtectEx(process, (nuint)address, 1, protection, out _))
                    failure = failure == null ? Error("Restore code protection") : new AggregateException(failure, Error("Restore code protection"));
            }
            if (failure != null) ExceptionDispatchInfo.Capture(failure).Throw();
            if (Query(process, address).Protect != protection || !ReadImage(rva, patched.Length).AsSpan().SequenceEqual(patched))
                throw new IOException("Patched method or page protection readback mismatch.");
            poisoned = false; written = true;
        }
        internal readonly record struct ThreadPosition(uint Id, ulong Instruction, ulong Stack);
        internal IReadOnlyList<ThreadPosition> GetThreadPositions()
        {
            Check();
            if (threads.Count == 0 || threads.Count > 512) throw new IOException("Invalid tracked thread count.");
            var result = new List<ThreadPosition>();
            foreach (var entry in threads)
            {
                nint raw = Marshal.AllocHGlobal(1248);
                nint aligned = (nint)((raw.ToInt64() + 15) & ~15L);
                try
                {
                    Marshal.Copy(new byte[1232], 0, aligned, 1232);
                    Marshal.WriteInt32(aligned, 48, (int)ContextControl);
                    if (!GetThreadContext(entry.Value, aligned)) throw Error("Read owned thread context");
                    if (((uint)Marshal.ReadInt32(aligned, 48) & ContextControl) != ContextControl) throw new IOException("Missing AMD64 control context.");
                    result.Add(new(entry.Key, (ulong)Marshal.ReadInt64(aligned, RipOffset), (ulong)Marshal.ReadInt64(aligned, RspOffset)));
                }
                finally { Marshal.FreeHGlobal(raw); }
            }
            return result;
        }
        internal void VerifyQuiescence(ulong method, uint length)
        {
            Check();
            if (length == 0) throw new ArgumentOutOfRangeException(nameof(length));
            ulong end = checked(method + length);
            foreach (var position in GetThreadPositions())
            {
                if (position.Instruction >= method && position.Instruction < end) throw new IOException("Thread instruction pointer lies inside method.");
                ulong cursor = position.Stack;
                if (cursor == 0 || (cursor & 7) != 0) throw new IOException("Invalid thread stack pointer.");
                var first = Query(process, cursor);
                ulong allocation = first.AllocationBase, scanned = 0;
                while (true)
                {
                    Check();
                    var region = Query(process, cursor);
                    if (region.AllocationBase != allocation) break;
                    if (region.Type != MemPrivate || region.State != MemCommit || !Readable(region.Protect)) throw new IOException("Unreadable active stack region.");
                    ulong regionEnd = checked((ulong)region.BaseAddress + (ulong)region.RegionSize);
                    if (regionEnd <= cursor) throw new IOException("Nonmonotone stack region.");
                    while (cursor < regionEnd)
                    {
                        Check();
                        int count = (int)Math.Min(8192UL, regionEnd - cursor);
                        if (scanned + (uint)count > 8 * 1024 * 1024) throw new IOException("Active stack scan budget exhausted.");
                        RejectStackReferences(ReadRaw(process, cursor, count), method, length);
                        scanned += (uint)count; cursor += (uint)count;
                    }
                }
            }
        }
        internal static void RejectStackReferences(ReadOnlySpan<byte> stack, ulong method, uint length)
        {
            ulong end = checked(method + length);
            for (int i = 0; i + 8 <= stack.Length; i += 8)
            {
                ulong value = BitConverter.ToUInt64(stack.Slice(i, 8));
                if (value >= method && value < end) throw new IOException("Possible in-flight return or exception address in active stack.");
            }
        }
    }

    private static uint ImageSize(nint process, ulong address)
    {
        byte[] header = ReadMapped(process, address, 4096, address);
        int nt = BitConverter.ToInt32(header, 60);
        if (BitConverter.ToUInt16(header, 0) != 0x5a4d || nt < 64 || nt > header.Length - 88 ||
            BitConverter.ToUInt32(header, nt) != 0x4550 || BitConverter.ToUInt16(header, nt + 4) != 0x8664 ||
            BitConverter.ToUInt16(header, nt + 24) != 0x20b) throw new IOException("Invalid loaded AMD64 image header.");
        uint size = BitConverter.ToUInt32(header, nt + 80);
        if (size < 4096 || size > 1024 * 1024 * 1024) throw new IOException("Invalid loaded image size.");
        return size;
    }
    private static MEMORY_BASIC_INFORMATION Query(nint process, ulong address)
    {
        if (VirtualQueryEx(process, (nuint)address, out var region, (nuint)Marshal.SizeOf<MEMORY_BASIC_INFORMATION>()) != (nuint)Marshal.SizeOf<MEMORY_BASIC_INFORMATION>()) throw Error("Query owned process memory");
        return region;
    }
    private static bool Readable(uint protection) => (protection & PageGuard) == 0 && (protection & 0xff) is 2 or 4 or 8 or 0x20 or 0x40 or 0x80;
    private static byte[] ReadMapped(nint process, ulong address, int count, ulong allocation)
    {
        ulong end = checked(address + (uint)count), cursor = address;
        while (cursor < end)
        {
            var region = Query(process, cursor);
            if (region.AllocationBase != allocation || region.State != MemCommit || region.Type != MemImage || !Readable(region.Protect))
                throw new IOException("Read is not a complete readable region of the expected image.");
            ulong next = checked((ulong)region.BaseAddress + (ulong)region.RegionSize);
            if (next <= cursor) throw new IOException("Invalid image memory region.");
            cursor = Math.Min(end, next);
        }
        return ReadRaw(process, address, count);
    }
    private static byte[] ReadRaw(nint process, ulong address, int count)
    {
        var bytes = new byte[count];
        if (!ReadProcessMemory(process, (nuint)address, bytes, (nuint)count, out nuint read) || read != (nuint)count) throw Error("Read owned process memory");
        return bytes;
    }
    private static string Normalize(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        if (path.Contains('\0')) throw new ArgumentException("Path contains NUL.");
        return Path.TrimEndingDirectorySeparator(Path.GetFullPath(path));
    }
    private static bool PathsEqual(string left, string right) => string.Equals(left, right, StringComparison.OrdinalIgnoreCase);
    private static FileStream OpenImage(string path, bool dll)
    {
        for (var directory = new DirectoryInfo(Path.GetDirectoryName(path)!); directory != null; directory = directory.Parent)
            if ((directory.Attributes & FileAttributes.ReparsePoint) != 0) throw new IOException("Linked startup paths are not supported.");
        if ((File.GetAttributes(path) & (FileAttributes.ReparsePoint | FileAttributes.Directory)) != 0) throw new IOException("Expected regular startup image.");
        var file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        try
        {
            using var pe = new PEReader(file, PEStreamOptions.LeaveOpen);
            if (pe.PEHeaders.IsCoffOnly || pe.PEHeaders.PEHeader?.Magic != PEMagic.PE32Plus || pe.PEHeaders.CoffHeader.Machine != Machine.Amd64 ||
                pe.PEHeaders.CorHeader != null || ((pe.PEHeaders.CoffHeader.Characteristics & Characteristics.Dll) != 0) != dll)
                throw new IOException("Expected a native AMD64 " + (dll ? "DLL." : "executable."));
            return file;
        }
        catch { file.Dispose(); throw; }
    }
    private static FileInformation Identity(nint handle)
    {
        if (handle == 0 || handle == -1 || !GetFileInformationByHandle(handle, out var info)) throw Error("Identify startup image handle");
        return info;
    }
    private static string FinalPath(nint handle)
    {
        var buffer = new StringBuilder(32768);
        uint count = GetFinalPathNameByHandleW(handle, buffer, (uint)buffer.Capacity, 0);
        if (count == 0 || count >= buffer.Capacity) throw Error("Resolve loaded image path");
        string path = buffer.ToString();
        if (path.StartsWith(@"\\?\UNC\", StringComparison.Ordinal)) path = @"\\" + path[8..];
        else if (path.StartsWith(@"\\?\", StringComparison.Ordinal)) path = path[4..];
        return Normalize(path);
    }
    private static void CloseEventFile(ref DebugEventInfo info)
    {
        if (info.Code is not (CreateProcessEvent or LoadDllEvent) || info.FileHandle == 0) return;
        nint file = info.FileHandle; info.FileHandle = 0;
        if (!CloseHandle(file)) throw Error("Close debug image file handle");
    }
    private static string Quote(string value)
    {
        if (value.Contains('\0')) throw new ArgumentException("Argument contains NUL.");
        var result = new StringBuilder("\""); int slashes = 0;
        foreach (char c in value)
        {
            if (c == '\\') { slashes++; continue; }
            result.Append('\\', c == '"' ? slashes * 2 + 1 : slashes); result.Append(c); slashes = 0;
        }
        return result.Append('\\', slashes * 2).Append('"').ToString();
    }
    private static string EnvironmentBlock(ProcessStartInfo start)
    {
        var result = new StringBuilder();
        foreach (var pair in start.Environment.OrderBy(p => p.Key, StringComparer.OrdinalIgnoreCase))
        {
            if (pair.Key.Length == 0 || pair.Key.Contains('=') || pair.Key.Contains('\0') || pair.Value?.Contains('\0') == true) throw new ArgumentException("Invalid child environment entry.");
            if (pair.Value != null) result.Append(pair.Key).Append('=').Append(pair.Value).Append('\0');
        }
        if (result.Length == 0) result.Append('\0');
        return result.Append('\0').ToString();
    }
    private static Win32Exception Error(string operation) => new(Marshal.GetLastWin32Error(), operation);
}
