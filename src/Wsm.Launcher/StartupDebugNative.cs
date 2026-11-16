using System.Runtime.InteropServices;
using System.Text;

namespace Wsm.Launcher;

// These AMD64 layouts/constants are checked against the installed Windows SDK.
internal static class StartupDebugNative
{
    internal const uint DBGContinue = 0x00010002, DBGExceptionNotHandled = 0x80010001;
    internal const uint ContextControl = 0x00100001;
    internal const uint CreateProcessEvent = 3, CreateThreadEvent = 2, ExitThreadEvent = 4,
        ExitProcessEvent = 5, LoadDllEvent = 6, UnloadDllEvent = 7, ExceptionEvent = 1;
    internal const uint MemCommit = 0x1000, MemImage = 0x1000000, MemPrivate = 0x20000;
    internal const uint PageGuard = 0x100, PageExecuteReadWrite = 0x40;
    [StructLayout(LayoutKind.Sequential)]
    internal struct PROCESS_INFORMATION { internal nint Process, Thread; internal uint ProcessId, ThreadId; }
    [StructLayout(LayoutKind.Sequential)]
    internal struct STARTUPINFOW
    {
        internal uint Size;
        internal nint Reserved, Desktop, Title;
        internal uint X, Y, Width, Height, XChars, YChars, Fill, Flags;
        internal ushort ShowWindow, ReservedBytes;
        internal nint ReservedData, Input, Output, Error;
    }
    [StructLayout(LayoutKind.Explicit, Size = 176)]
    internal struct DebugEventInfo
    {
        [FieldOffset(0)] internal uint Code;
        [FieldOffset(4)] internal uint ProcessId;
        [FieldOffset(8)] internal uint ThreadId;
        [FieldOffset(16)] internal nint FileHandle;
        [FieldOffset(24)] internal nint ProcessHandle;
        [FieldOffset(32)] internal nint MainThreadHandle;
        [FieldOffset(40)] internal nint MainImageBase;
        [FieldOffset(24)] internal nint DllBase;
        [FieldOffset(16)] internal nint ThreadHandle;
        [FieldOffset(16)] internal nint UnloadBase;
        [FieldOffset(16)] internal uint ExitCode;
        [FieldOffset(16)] internal uint ExceptionCode;
        [FieldOffset(20)] internal uint ExceptionFlags;
        [FieldOffset(32)] internal nint ExceptionAddress;
        [FieldOffset(168)] internal uint FirstChance;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct MEMORY_BASIC_INFORMATION
    {
        internal nuint BaseAddress, AllocationBase;
        internal uint AllocationProtect, PartitionPadding;
        internal nuint RegionSize;
        internal uint State, Protect, Type;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct FileInformation
    {
        internal uint Attributes, CreationLow, CreationHigh, AccessLow, AccessHigh,
            WriteLow, WriteHigh, Volume, SizeHigh, SizeLow, Links, IndexHigh, IndexLow;
    }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool CreateProcessW(string application, StringBuilder command, nint processAttributes,
        nint threadAttributes, [MarshalAs(UnmanagedType.Bool)] bool inherit, uint flags, nint environment,
        string workingDirectory, ref STARTUPINFOW startup, out PROCESS_INFORMATION process);
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool WaitForDebugEvent(out DebugEventInfo debugEvent, uint milliseconds);
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool ContinueDebugEvent(uint process, uint thread, uint status);
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool DebugActiveProcessStop(uint process);
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool TerminateProcess(nint process, uint exitCode);
    [DllImport("kernel32.dll", SetLastError = true)]
    internal static extern uint WaitForSingleObject(nint handle, uint milliseconds);
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool CloseHandle(nint handle);
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool ReadProcessMemory(nint process, nuint address, [Out] byte[] buffer, nuint bytes, out nuint read);
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool WriteProcessMemory(nint process, nuint address, byte[] buffer, nuint bytes, out nuint written);
    [DllImport("kernel32.dll", SetLastError = true)]
    internal static extern nuint VirtualQueryEx(nint process, nuint address, out MEMORY_BASIC_INFORMATION memory, nuint size);
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool VirtualProtectEx(nint process, nuint address, nuint size, uint protection, out uint previous);
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool FlushInstructionCache(nint process, nuint address, nuint size);
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool GetThreadContext(nint thread, nint context);
    [DllImport("kernel32.dll")]
    internal static extern uint GetCurrentThreadId();
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool GetFileInformationByHandle(nint file, out FileInformation information);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    internal static extern uint GetFinalPathNameByHandleW(nint file, StringBuilder path, uint size, uint flags);
}
