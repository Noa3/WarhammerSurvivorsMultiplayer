using System.Buffers.Binary;
using System.Diagnostics;
using System.Reflection.PortableExecutable;
using System.Runtime.InteropServices;
using Wsm.Launcher;

internal static class StartupDebugRegression
{
    internal static void Run(string fixtureDirectory)
    {
        int count = 0;
        void Case(string name, Action action) { action(); ++count; Console.WriteLine("case PASS " + name); }
        Case("windows_x64_abi_contract", () =>
        {
            Require(StartupDebugNative.DBGContinue == 0x00010002 && StartupDebugNative.DBGExceptionNotHandled == 0x80010001, "Wrong debug continuation constants");
            Require(Marshal.SizeOf<StartupDebugNative.DebugEventInfo>() == 176 && Marshal.SizeOf<StartupDebugNative.STARTUPINFOW>() == 104 &&
                Marshal.SizeOf<StartupDebugNative.MEMORY_BASIC_INFORMATION>() == 48, "Wrong Win32 struct layouts");
            Require(StartupDebugProcess.StoppedImage.RspOffset == 152 && StartupDebugNative.ContextControl == 0x00100001, "Wrong AMD64 context layout");
        });
        string host = Path.Combine(fixtureDirectory, "startup_debug_host.exe"), dll = Path.Combine(fixtureDirectory, "StartupDebugFixture.dll");
        uint rva = Export(dll, "StartupDebugTarget");
        byte[] original;
        using (var pe = new PEReader(File.OpenRead(dll))) original = pe.GetSectionData((int)rva).GetContent(0, 6).ToArray();
        Require(original.SequenceEqual(new byte[] { 0xb8, 1, 0, 0, 0, 0xc3 }), "Unexpected own fixture machine code");
        var patched = (byte[])original.Clone(); patched[1] = 2;
        ProcessStartInfo Request(string? mode = null)
        {
            var request = new ProcessStartInfo(host) { UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = fixtureDirectory };
            if (mode != null) request.ArgumentList.Add(mode);
            return request;
        }
        Process Start(Action<StartupDebugProcess.StoppedImage> apply, string? mode = null) =>
            StartupDebugProcess.Start(Request(mode), dll, apply, CancellationToken.None, TimeSpan.FromSeconds(10));
        Case("positive_one_byte_patch", () =>
        {
            using var child = Start(image =>
            {
                Require(image.ReadImage(rva, 6).SequenceEqual(original), "Original readback mismatch");
                image.ReplaceCodeByte(rva, 1, original, patched);
                Require(image.ReadImage(rva, 6).SequenceEqual(patched), "Patched readback mismatch");
            });
            Exit(child, 0);
        });
        Case("negative_no_patch_returns_one", () => { using var child = Start(_ => { }); Exit(child, 101); });
        Case("callback_exception_terminates_owned_child", () => CleanFailure(Throws<InvalidOperationException>(() =>
            { using var child = Start(_ => throw new InvalidOperationException("controlled callback")); })));
        Case("wrong_original_refuses_without_leaking_child", () =>
        {
            var wrong = (byte[])original.Clone(); wrong[0] ^= 1;
            var wrongPatched = (byte[])wrong.Clone(); wrongPatched[1] = 2;
            var error = Throws<IOException>(() => { using var child = Start(image => image.ReplaceCodeByte(rva, 1, wrong, wrongPatched)); });
            Require(error.Message.Contains("original method mismatch"), "Wrong method refusal boundary");
            CleanFailure(error);
        });
        Case("multibyte_patch_refuses_without_leaking_child", () =>
        {
            var wrong = (byte[])patched.Clone(); wrong[2] ^= 1;
            CleanFailure(Throws<ArgumentException>(() => { using var child = Start(image => image.ReplaceCodeByte(rva, 1, original, wrong)); }));
        });
        Case("cross_thread_certificate_rejected_then_valid_patch", () =>
        {
            using var child = Start(image =>
            {
                Exception? captured = null;
                var other = new Thread(() => { try { image.ReadImage(rva, 6); } catch (Exception error) { captured = error; } });
                other.Start(); Require(other.Join(5000), "Foreign certificate probe did not finish");
                Require(captured is InvalidOperationException && captured.Message.Contains("certificate"), "Foreign certificate accepted");
                image.ReplaceCodeByte(rva, 1, original, patched);
            });
            Exit(child, 0);
        });
        Case("expired_certificate_rejects_after_detach", () =>
        {
            StartupDebugProcess.StoppedImage? saved = null;
            using var child = Start(image => saved = image); Exit(child, 101);
            Throws<InvalidOperationException>(() => saved!.ReadImage(rva, 6));
        });
        Case("cancellation_at_callback_cleans_owned_child", () =>
        {
            using var cancel = new CancellationTokenSource();
            CleanFailure(Throws<OperationCanceledException>(() =>
            {
                using var child = StartupDebugProcess.Start(Request(), dll, image => { cancel.Cancel(); image.ReadImage(rva, 6); }, cancel.Token, TimeSpan.FromSeconds(10));
            }));
        });
        Case("timeout_without_image_cleans_owned_child", () => CleanFailure(Throws<TimeoutException>(() =>
        {
            using var child = StartupDebugProcess.Start(Request("--no-image"), dll, _ => { }, CancellationToken.None, TimeSpan.FromMilliseconds(500));
        })));
        Case("cancellation_after_patch_still_terminates_before_release", () =>
        {
            using var cancel = new CancellationTokenSource();
            CleanFailure(Throws<OperationCanceledException>(() =>
            {
                using var child = StartupDebugProcess.Start(Request(), dll, image =>
                { image.ReplaceCodeByte(rva, 1, original, patched); cancel.Cancel(); }, cancel.Token, TimeSpan.FromSeconds(10));
            }));
        });
        Case("natural_exit_without_image_is_reported_and_clean", () => CleanFailure(Throws<IOException>(() =>
        {
            using var child = StartupDebugProcess.Start(Request("--exit"), dll, _ => { }, CancellationToken.None, TimeSpan.FromSeconds(5));
        })));
        Case("live_instruction_and_stack_admission", () =>
        {
            using var child = Start(image =>
            {
                var positions = image.GetThreadPositions(); Require(positions.Count >= 2, "Missing extra live thread");
                image.VerifyQuiescence(image.ImageBase + rva, 6);
                var error = Throws<IOException>(() => image.VerifyQuiescence(positions[0].Instruction, 1));
                Require(error.Message.Contains("instruction pointer"), "Wrong instruction-range refusal");
                byte[] words = new byte[16];
                BinaryPrimitives.WriteUInt64LittleEndian(words, image.ImageBase + rva + 6);
                StartupDebugProcess.StoppedImage.RejectStackReferences(words, image.ImageBase + rva, 6);
                BinaryPrimitives.WriteUInt64LittleEndian(words, image.ImageBase + rva + 1);
                Require(Throws<IOException>(() => StartupDebugProcess.StoppedImage.RejectStackReferences(words, image.ImageBase + rva, 6)).Message.Contains("in-flight"), "Wrong stack-range refusal");
                image.ReplaceCodeByte(rva, 1, original, patched);
            }, "--admission");
            Exit(child, 0);
        });
        Case("argument_bounds_and_single_write_limit", () =>
        {
            using var child = Start(image =>
            {
                Throws<ArgumentNullException>(() => image.ReplaceCodeByte(rva, 1, null!, patched));
                Throws<ArgumentOutOfRangeException>(() => image.ReadImage(uint.MaxValue, 6));
                Throws<ArgumentOutOfRangeException>(() => image.ReplaceCodeByte(rva, 6, original, patched));
                Throws<ArgumentException>(() => image.ReplaceCodeByte(rva, 0, original, patched));
                image.ReplaceCodeByte(rva, 1, original, patched);
                Throws<InvalidOperationException>(() => image.ReplaceCodeByte(rva, 1, patched, original));
            });
            Exit(child, 0);
        });
        Case("windows_argument_and_unicode_environment_roundtrip", () =>
        {
            var request = Request("--arguments");
            foreach (var value in new[] { "", "space value", "quote\"inside", "tail\\" }) request.ArgumentList.Add(value);
            request.Environment["WSM_FIXTURE_VALUE"] = "unicode_ä";
            using var child = StartupDebugProcess.Start(request, dll, image => image.ReplaceCodeByte(rva, 1, original, patched), CancellationToken.None, TimeSpan.FromSeconds(10));
            Exit(child, 0);
        });
        Case("prelaunch_validation_never_creates_child", () =>
        {
            using var cancel = new CancellationTokenSource(); cancel.Cancel();
            Throws<OperationCanceledException>(() => { using var p = StartupDebugProcess.Start(Request(), dll, _ => { }, cancel.Token, TimeSpan.FromSeconds(1)); });
            Throws<ArgumentOutOfRangeException>(() => { using var p = StartupDebugProcess.Start(Request(), dll, _ => { }, CancellationToken.None, TimeSpan.Zero); });
            var bad = Request(); bad.UseShellExecute = true;
            Throws<ArgumentException>(() => { using var p = StartupDebugProcess.Start(bad, dll, _ => { }, CancellationToken.None, TimeSpan.FromSeconds(1)); });
            Throws<ArgumentException>(() => { using var p = StartupDebugProcess.Start(Request(), dll + "\0bad", _ => { }, CancellationToken.None, TimeSpan.FromSeconds(1)); });
        });
        Case("repeated_detach_and_failure_handles_stay_bounded", () =>
        {
            GC.Collect(); GC.WaitForPendingFinalizers();
            using var process = Process.GetCurrentProcess();process.Refresh();int before = process.HandleCount;
            for (int i = 0; i < 8; ++i)
            {
                using var child = Start(image => image.ReplaceCodeByte(rva, 1, original, patched));Exit(child, 0);
                CleanFailure(Throws<InvalidOperationException>(() => { using var failed = Start(_ => throw new InvalidOperationException("handle probe")); }));
            }
            GC.Collect(); GC.WaitForPendingFinalizers();process.Refresh();int after = process.HandleCount;
            Require(after <= before + 4, $"Handle growth across own fixture runs: {before}->{after}");
            Console.WriteLine($"STARTUP_DEBUG_HANDLES before={before} after={after}");
        });
        Console.WriteLine("STARTUP_DEBUG_CASES=" + count);
    }
    private static void Exit(Process child, int expected)
    {
        try { Require(child.WaitForExit(10000) && child.ExitCode == expected, "Own fixture exit mismatch, expected " + expected); }
        finally { if (!child.HasExited) { child.Kill(entireProcessTree: false); Require(child.WaitForExit(5000), "Own fixture teardown failed"); } }
    }
    private static void CleanFailure(Exception error)
    {
        Require(error.Data["OwnedChildPid"] is uint pid && pid != 0 && error.Data["OwnedChildExitConfirmed"] is true, "Owned failure cleanup not confirmed: " + error);
    }
    private static void Require(bool condition, string error) { if (!condition) throw new Exception(error); }
    private static T Throws<T>(Action action) where T : Exception
    {
        try { action(); } catch (T error) { return error; }
        throw new Exception("Expected " + typeof(T).Name);
    }
    private static uint Export(string file, string name)
    {
        using var pe = new PEReader(File.OpenRead(file));
        byte[] Read(uint rva, int count) => pe.GetSectionData(checked((int)rva)).GetContent(0, count).ToArray();
        var directory = pe.PEHeaders.PEHeader!.ExportTableDirectory;
        byte[] table = Read((uint)directory.RelativeVirtualAddress, 40);
        uint count = BitConverter.ToUInt32(table, 24), functions = BitConverter.ToUInt32(table, 28), names = BitConverter.ToUInt32(table, 32), ordinals = BitConverter.ToUInt32(table, 36);
        Require(count is > 0 and < 256, "Invalid own fixture exports");
        for (uint i = 0; i < count; ++i)
        {
            uint text = BitConverter.ToUInt32(Read(names + i * 4, 4));
            var bytes = new List<byte>();
            for (int j = 0; j < 128; ++j) { byte b = Read(text + (uint)j, 1)[0]; if (b == 0) break; bytes.Add(b); }
            if (System.Text.Encoding.ASCII.GetString(bytes.ToArray()) != name) continue;
            ushort ordinal = BitConverter.ToUInt16(Read(ordinals + i * 2, 2));
            return BitConverter.ToUInt32(Read(functions + (uint)ordinal * 4, 4));
        }
        throw new Exception("Missing own fixture export");
    }
}
