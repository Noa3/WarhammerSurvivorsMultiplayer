using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Channels;

namespace Wsm.InGame.Runtime;

/// <summary>
/// C ABI for the native in-game menu. Every export validates its arguments before touching
/// caller memory, never lets an exception cross the ABI, and returns immediately: session work
/// happens on background tasks so the Unity main thread is never blocked.
/// </summary>
public static class NativeExports
{
    /// <summary>Accepted by the command exports: the request was queued, not completed.</summary>
    public const int Accepted = 0;
    /// <summary>Invalid argument (port range, IPv4 text, negative capacity, null buffer).</summary>
    public const int InvalidArgument = -1;
    /// <summary>Runtime unavailable or an internal failure was contained.</summary>
    public const int Unavailable = -2;
    /// <summary>Command queue is full; retry after the next poll.</summary>
    public const int QueueFull = -3;
    /// <summary>Destination capacity is smaller than the required length plus terminator.</summary>
    public const int InsufficientCapacity = -5;

    private enum CommandKind { Host, Join }

    private readonly record struct Command(CommandKind Kind, int Port, bool LanBind, string Address, long Generation, CancellationToken Cancellation);

    private static readonly IngameSessionController Controller = new();
    internal static IngameSessionController ControllerForTests => Controller;
    private static readonly Channel<Command> Queue =
        Channel.CreateBounded<Command>(new BoundedChannelOptions(8) { FullMode = BoundedChannelFullMode.Wait, SingleReader = true });
    private static readonly object StatusGate = new();
    private static readonly Lazy<Task> Worker = new(StartWorker, LazyThreadSafetyMode.ExecutionAndPublication);
    private static IngameStatus statusCache = IngameStatus.Idle;
    private static byte[] statusBytes = IngameStatusText.ToBoundedUtf8(IngameStatus.Idle.Text);
    private static byte[] errorBytes = [];
    private static int stopped;
    private static readonly object CommandGate = new();
    private static long commandGeneration;
    private static Task leaveBarrier = Task.CompletedTask;
    private static CancellationTokenSource commandCancellation = new();

    /// <summary>ABI revision of this library.</summary>
    [UnmanagedCallersOnly(EntryPoint = "wsm_ingame_version", CallConvs = [typeof(CallConvCdecl)])]
    public static int Version() => 1;

    /// <summary>Optional gameplay mailbox contract; the original connection ABI stays at 1.</summary>
    [UnmanagedCallersOnly(EntryPoint = "wsm_ingame_gameplay_version", CallConvs = [typeof(CallConvCdecl)])]
    public static int GameplayVersion() => 1;

    [UnmanagedCallersOnly(EntryPoint = "wsm_ingame_gameplay_epoch", CallConvs = [typeof(CallConvCdecl)])]
    public static int GameplayEpoch()
    { try { return Volatile.Read(ref stopped) == 0 ? Controller.GameplayEpoch : 0; } catch { return 0; } }

    [UnmanagedCallersOnly(EntryPoint = "wsm_ingame_gameplay_info", CallConvs = [typeof(CallConvCdecl)])]
    public static unsafe int GameplayInfo(byte* buffer, int capacity)
    { try { return WriteUtf8(Controller.GameplayInfoPayload(), buffer, capacity); } catch { return Unavailable; } }

    /// <summary>Queues validated UTF-8 JSON for this epoch; never waits for network I/O.</summary>
    [UnmanagedCallersOnly(EntryPoint = "wsm_ingame_gameplay_submit", CallConvs = [typeof(CallConvCdecl)])]
    public static unsafe int GameplaySubmit(int expectedEpoch, byte* json, int length)
    {
        try
        {
            if (Volatile.Read(ref stopped) != 0) return Unavailable;
            if (expectedEpoch <= 0 || json is null || length is < 1 or > GameplayMailbox.MaxRequestBytes) return InvalidArgument;
            return Controller.SubmitGameplay(expectedEpoch, new ReadOnlySpan<byte>(json, length));
        }
        catch { return Unavailable; }
    }

    /// <summary>Queries or copies+consumes one event. Empty=0; -5 leaves it queued.</summary>
    [UnmanagedCallersOnly(EntryPoint = "wsm_ingame_gameplay_poll", CallConvs = [typeof(CallConvCdecl)])]
    public static unsafe int GameplayPoll(byte* buffer, int capacity)
    {
        try
        {
            if (Volatile.Read(ref stopped) != 0) return Unavailable;
            if (capacity < 0 || capacity > GameplayMailbox.MaxEventBytes + 1 || (buffer is null && capacity != 0) ||
                (buffer is not null && capacity == 0)) return InvalidArgument;
            return Controller.PollGameplay(buffer is null ? Span<byte>.Empty : new Span<byte>(buffer, capacity));
        }
        catch { return Unavailable; }
    }

    /// <summary>Starts hosting in the current process. port 0 binds an ephemeral port.</summary>
    [UnmanagedCallersOnly(EntryPoint = "wsm_ingame_host", CallConvs = [typeof(CallConvCdecl)])]
    public static int Host(int port, int lanBind)
    {
        try
        {
            if (Volatile.Read(ref stopped) != 0) return Unavailable;
            if (port is < 0 or > 65535 || lanBind is not (0 or 1)) return InvalidArgument;
            EnsureWorker();
            lock (CommandGate)
                return Queue.Writer.TryWrite(new Command(CommandKind.Host, port, lanBind == 1, "", commandGeneration, commandCancellation.Token)) ? Accepted : QueueFull;
        }
        catch { return Unavailable; }
    }

    /// <summary>Joins a host by IPv4 literal. No DNS lookup is performed.</summary>
    [UnmanagedCallersOnly(EntryPoint = "wsm_ingame_join", CallConvs = [typeof(CallConvCdecl)])]
    public static unsafe int Join(byte* ipUtf8, int ipLength, int port)
    {
        try
        {
            if (Volatile.Read(ref stopped) != 0) return Unavailable;
            if (ipUtf8 is null || ipLength is < 1 or > 15 || port is < 1 or > 65535) return InvalidArgument;
            for (var i = 0; i < ipLength; i++)
            {
                var value = ipUtf8[i];
                if (value is not ((>= (byte)'0' and <= (byte)'9') or (byte)'.')) return InvalidArgument;
            }
            var address = Encoding.ASCII.GetString(ipUtf8, ipLength);
            if (!IngameSessionController.TryParseIpv4(address, out _)) return InvalidArgument;
            EnsureWorker();
            lock (CommandGate)
                return Queue.Writer.TryWrite(new Command(CommandKind.Join, port, false, address, commandGeneration, commandCancellation.Token)) ? Accepted : QueueFull;
        }
        catch { return Unavailable; }
    }

    /// <summary>Leaves the current lobby, including a join that is still in flight.</summary>
    [UnmanagedCallersOnly(EntryPoint = "wsm_ingame_leave", CallConvs = [typeof(CallConvCdecl)])]
    public static int Leave()
    {
        try
        {
            if (Volatile.Read(ref stopped) != 0) return Unavailable;
            ScheduleDeparture();
            return Accepted;
        }
        catch { return Unavailable; }
    }

    private static Task ScheduleDeparture()
    {
        lock (CommandGate)
        {
            ++commandGeneration;
            var preceding = leaveBarrier;
            var oldCancellation = commandCancellation;
            commandCancellation = new CancellationTokenSource();
            // CancelAsync marks the epoch cancelled immediately, while callbacks and
            // controller cleanup run off the native/Unity caller's thread.
            var cancellation = oldCancellation.CancelAsync();
            return leaveBarrier = Task.Run(async () =>
            {
                try { await preceding.ConfigureAwait(false); }
                catch (Exception error) { System.Diagnostics.Trace.TraceError("Retrying failed departure: {0}", error.GetType().Name); }
                try
                {
                    Exception? cancellationError = null;
                    try { await cancellation.ConfigureAwait(false); }
                    catch (Exception error) { cancellationError = error; }
                    await Controller.LeaveAsync().ConfigureAwait(false);
                    if (cancellationError is not null) throw new AggregateException("Cancellation callback failed after departure cleanup.", cancellationError);
                }
                finally { oldCancellation.Dispose(); }
            });
        }
    }

    /// <summary>
    /// Required UTF-8 byte length excluding the terminator when buffer is null and capacity is 0.
    /// Writes the text plus NUL when the capacity suffices. Status text is capped at 4096 bytes.
    /// </summary>
    [UnmanagedCallersOnly(EntryPoint = "wsm_ingame_status", CallConvs = [typeof(CallConvCdecl)])]
    public static unsafe int Status(byte* buffer, int capacity)
    {
        try { return WriteUtf8(StatusPayload(), buffer, capacity); }
        catch { return Unavailable; }
    }

    /// <summary>Same contract as wsm_ingame_status; ASCII failure category, empty when none.</summary>
    [UnmanagedCallersOnly(EntryPoint = "wsm_ingame_last_error", CallConvs = [typeof(CallConvCdecl)])]
    public static unsafe int LastError(byte* buffer, int capacity)
    {
        try { return WriteUtf8(ErrorPayload(), buffer, capacity); }
        catch { return Unavailable; }
    }

    /// <summary>0 disconnected, 1 hosting, 2 connecting, 3 connected, 4 failed.</summary>
    [UnmanagedCallersOnly(EntryPoint = "wsm_ingame_state", CallConvs = [typeof(CallConvCdecl)])]
    public static int State()
    {
        try { return (int)Controller.Status.State; } catch { return Unavailable; }
    }

    /// <summary>Actually connected peers: hosted guests, or 1 for a connected guest.</summary>
    [UnmanagedCallersOnly(EntryPoint = "wsm_ingame_peer_count", CallConvs = [typeof(CallConvCdecl)])]
    public static int PeerCount()
    {
        try { return Controller.Status.PeerCount; } catch { return 0; }
    }

    /// <summary>Listening port while hosting (resolves ephemeral port 0), otherwise 0.</summary>
    [UnmanagedCallersOnly(EntryPoint = "wsm_ingame_bound_port", CallConvs = [typeof(CallConvCdecl)])]
    public static int BoundPort()
    {
        try { return Controller.Status.BoundPort; } catch { return 0; }
    }

    /// <summary>
    /// Test/teardown helper: leaves, stops the worker and releases the listening socket.
    /// Native AOT libraries cannot be unloaded, so the module stays mapped for the process lifetime.
    /// </summary>
    [UnmanagedCallersOnly(EntryPoint = "wsm_ingame_shutdown", CallConvs = [typeof(CallConvCdecl)])]
    public static int Shutdown()
    {
        try
        {
            if (Interlocked.Exchange(ref stopped, 1) != 0) return Accepted;
            var leave = ScheduleDeparture();
            if (!leave.Wait(TimeSpan.FromSeconds(10))) return Unavailable;
            Queue.Writer.TryComplete();
            if (Worker.IsValueCreated && !Worker.Value.Wait(TimeSpan.FromSeconds(5))) return Unavailable;
            return Accepted;
        }
        catch { return Unavailable; }
    }

    private static unsafe int WriteUtf8(byte[] bytes, byte* buffer, int capacity)
    {
        if (capacity < 0) return InvalidArgument;
        if (buffer is null) return capacity == 0 ? bytes.Length : InvalidArgument;
        if (capacity < bytes.Length + 1) return InsufficientCapacity;
        for (var i = 0; i < bytes.Length; i++) buffer[i] = bytes[i];
        buffer[bytes.Length] = 0;
        return bytes.Length;
    }

    private static byte[] StatusPayload()
    {
        var status = Controller.Status;
        lock (StatusGate)
        {
            Refresh(status);
            return statusBytes;
        }
    }

    private static byte[] ErrorPayload()
    {
        var status = Controller.Status;
        lock (StatusGate)
        {
            Refresh(status);
            return errorBytes;
        }
    }

    private static void Refresh(IngameStatus status)
    {
        if (ReferenceEquals(status, statusCache)) return;
        statusCache = status;
        statusBytes = IngameStatusText.ToBoundedUtf8(status.Text);
        errorBytes = Encoding.ASCII.GetBytes(status.FailureCode);
    }

    private static void EnsureWorker() => _ = Worker.Value;

    private static Task StartWorker() => Task.Run(async () =>
    {
        try
        {
            await foreach (var command in Queue.Reader.ReadAllAsync().ConfigureAwait(false))
            {
                try
                {
                    Task barrier;
                    lock (CommandGate)
                    {
                        if (command.Generation != commandGeneration) continue;
                        barrier = leaveBarrier;
                    }
                    await barrier.ConfigureAwait(false);
                    Task<TransitionResult>? operation;
                    lock (CommandGate)
                    {
                        if (command.Generation != commandGeneration) continue;
                        operation = command.Kind == CommandKind.Host
                            ? Controller.HostAsync(command.Port, command.LanBind, command.Cancellation)
                            : Controller.JoinAsync(command.Address, command.Port, command.Cancellation);
                    }
                    await operation.ConfigureAwait(false);
                }
                catch (Exception)
                {
                    // A failed transition is already reflected in the status; never kill the worker.
                }
            }
        }
        catch (Exception) { }
    });
}
