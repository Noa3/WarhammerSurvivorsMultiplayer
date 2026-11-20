using System.Net;
using System.Net.Sockets;
using System.Threading.Channels;
using Wsm.Core;

namespace Wsm.InGame.Runtime;

/// <summary>Outcome of a start/join request. <see cref="Started"/> means installed, not merely queued.</summary>
public enum TransitionResult
{
    Started = 0,
    InvalidArgument = 1,
    Busy = 2,
    Failed = 3,
    Superseded = 4,
}

/// <summary>
/// Owns exactly one Wsm.Core TCP session (host or guest) inside the game process.
/// Every public method is safe to call from the Unity main thread: connection setup and all
/// receive loops run on background tasks, and <see cref="Status"/> is an immutable snapshot.
/// The type exposes bounded gameplay mailboxes, but never calls engine APIs,
/// starts gameplay or applies bot decisions to engine actors.
/// </summary>
public sealed partial class IngameSessionController : IAsyncDisposable
{
    /// <summary>Default bind port used by the native in-game menu.</summary>
    public const int DefaultPort = 7780;
    /// <summary>Guests allowed on top of the host, so a full lobby is four players.</summary>
    public const int MaxClients = 3;
    /// <summary>Connect/handshake deadline for a join request.</summary>
    public static readonly TimeSpan ConnectTimeout = TimeSpan.FromSeconds(8);
    /// <summary>Trusted-LAN cleartext session; the UI warns about it and no secret is reused.</summary>
    internal const string SessionPassword = "";

    private readonly SessionSettings settings;
    private readonly object gate = new();
    private readonly SemaphoreSlim transition = new(1, 1);
    private readonly SemaphoreSlim leaveGate = new(1, 1);
    private Session? active;
    private Session? pendingRelease;
    private int generation;
    private IngameStatus current = IngameStatus.Idle;
    private int disposed;
    private Task? disposal;
    internal Action? BeforeResourceCommit { get; set; }
    internal Action? BeforeOperationStart { get; set; }
    internal Action? BeforeSessionAdmission { get; set; }
    internal Action? AfterResourcesReleased { get; set; }
    internal Func<string, TimeSpan>? StallGameplayChannelForTests { get; set; }

    public IngameSessionController(SessionSettings? settings = null) =>
        this.settings = settings ?? new SessionSettings(MaxClients, DeathMode.AllDead, 0, BotDifficulty.Normal);

    /// <summary>Most recent immutable status. Polling this from any thread is safe.</summary>
    public IngameStatus Status => Volatile.Read(ref current);
    internal byte[] LanHint
    {
        get { lock (gate) return current.State == IngameState.Hosting && active is { } session ? session.LanHint : []; }
    }
    /// <summary>Connected human display names, host first; no gameplay events are consumed.</summary>
    public string PlayerNames
    {
        get
        {
            lock (gate)
            {
                if (active is not { } session || current.State is not (IngameState.Hosting or IngameState.Connected)) return "";
                var names = session.Host?.PlayerNames ?? session.Client?.PlayerNames ?? [];
                return string.Join("\n", names.Select(p => p.Name));
            }
        }
    }
    public string LocalPlayerName
    {
        get
        {
            lock (gate)
            {
                if (active is not { } session || current.State is not (IngameState.Hosting or IngameState.Connected)) return "";
                return session.Client is { } client && client.PlayerNames.Length == 0 ? "" : session.Name;
            }
        }
    }

    private sealed class Session(int generation, string name)
    {
        public int Generation { get; } = generation;
        public string Name { get; } = name;
        public GameplayMailbox Gameplay { get; } = new(generation);
        public ObservedBotRoster BotRoster { get; } = new();
        public Channel<GameplayRequest> GameplayOutgoing { get; } = Channel.CreateBounded<GameplayRequest>(
            new BoundedChannelOptions(32) { FullMode = BoundedChannelFullMode.Wait, SingleReader = true });
        public CancellationTokenSource Cancellation { get; } = new();
        public SessionHost? Host { get; set; }
        public byte[] LanHint { get; set; } = [];
        public SessionClient? Client { get; set; }
        public Task Pump { get; set; } = Task.CompletedTask;
        public object ReleaseGate { get; } = new();
        public Task? Release { get; set; }
        public Task LossCleanup { get; set; } = Task.CompletedTask;
        private int abandoned;
        public bool Abandoned
        {
            get => Volatile.Read(ref abandoned) != 0;
            set => Volatile.Write(ref abandoned, value ? 1 : 0);
        }
    }

    /// <summary>Starts hosting. A port of 0 binds an ephemeral port; the result is in <see cref="Status"/>.</summary>
    public Task<TransitionResult> HostAsync(int port, bool bindToLan, CancellationToken token = default, string? displayName = null) =>
        HostAsync(port, bindToLan, token, displayName, null);

    internal Task<TransitionResult> HostAsync(int port, bool bindToLan, CancellationToken token, string? displayName, SessionSettings? requested) =>
        Task.Run(() => { BeforeOperationStart?.Invoke(); return HostCoreAsync(port, bindToLan, token, DisplayNames.Sanitize(displayName), requested ?? settings); });

    private async Task<TransitionResult> HostCoreAsync(int port, bool bindToLan, CancellationToken token, string displayName, SessionSettings hostSettings)
    {
        if (port is < 0 or > 65535) return TransitionResult.InvalidArgument;
        if (Volatile.Read(ref disposed) != 0) return TransitionResult.Failed;
        if (token.IsCancellationRequested) return TransitionResult.Superseded;
        if (!transition.Wait(0)) return TransitionResult.Busy;
        try
        {
            BeforeSessionAdmission?.Invoke();
            Session session;
            lock (gate)
            {
                if (disposed != 0) return TransitionResult.Failed;
                if (active is not null || pendingRelease is not null || retainedGameplay?.HasReliable == true) return TransitionResult.Busy;
                retainedGameplay = null;
                session = new Session(++generation, displayName);
                active = session;
            }
            var endpoint = $"{(bindToLan ? "0.0.0.0" : "127.0.0.1")}:{port}";
            PublishFor(session, IngameState.Connecting, IngameFailure.None, 0, 0, endpoint, bindToLan);
            try
            {
                token.ThrowIfCancellationRequested();
                var host = new SessionHost(hostSettings, SessionPassword, port, bindToLan ? IPAddress.Any : IPAddress.Loopback, displayName);
                session.Host = host; // Own the object even when Start or the admission step fails.
                host.Start();
                if (bindToLan) session.LanHint = LanEndpointHint.Discover(host.Port);
                BeforeResourceCommit?.Invoke();
                bool owned;
                bool cancelled;
                lock (gate)
                {
                    session.Host = host;
                    owned = ReferenceEquals(active, session);
                    cancelled = token.IsCancellationRequested;

                }
                if (!owned || cancelled)
                {
                    if (owned) await FinishFailureAsync(session, IngameFailure.None, endpoint, bindToLan).ConfigureAwait(false);
                    return TransitionResult.Superseded;
                }
                session.Pump = Task.Run(() => HostPumpAsync(session, host));
                PublishFor(session, IngameState.Hosting, IngameFailure.None, host.ConnectedClients, host.Port, endpoint, bindToLan);
                return TransitionResult.Started;
            }
            catch (OperationCanceledException)
            {
                await FinishFailureAsync(session, IngameFailure.None, endpoint, bindToLan).ConfigureAwait(false);
                return TransitionResult.Superseded;
            }
            catch (SocketException ex)
            {
                var failure = ex.SocketErrorCode is SocketError.AddressAlreadyInUse or SocketError.AccessDenied
                    ? IngameFailure.PortInUse
                    : IngameFailure.Internal;
                await FinishFailureAsync(session, failure, endpoint, bindToLan).ConfigureAwait(false);
                return TransitionResult.Failed;
            }
            catch (Exception)
            {
                await FinishFailureAsync(session, IngameFailure.Internal, endpoint, bindToLan).ConfigureAwait(false);
                return TransitionResult.Failed;
            }
        }
        finally
        {
            transition.Release();
        }
    }

    /// <summary>Joins a host by IPv4 literal. DNS is intentionally unsupported.</summary>
    public Task<TransitionResult> JoinAsync(string address, int port, CancellationToken token = default, string? displayName = null) =>
        Task.Run(() => JoinCoreAsync(address, port, token, DisplayNames.Sanitize(displayName)));

    private async Task<TransitionResult> JoinCoreAsync(string address, int port, CancellationToken token, string displayName)
    {
        if (!TryParseIpv4(address, out var ipAddress) || port is < 1 or > 65535) return TransitionResult.InvalidArgument;
        if (Volatile.Read(ref disposed) != 0) return TransitionResult.Failed;
        if (token.IsCancellationRequested) return TransitionResult.Superseded;
        if (!transition.Wait(0)) return TransitionResult.Busy;
        try
        {
            BeforeSessionAdmission?.Invoke();
            Session session;
            lock (gate)
            {
                if (disposed != 0) return TransitionResult.Failed;
                if (active is not null || pendingRelease is not null || retainedGameplay?.HasReliable == true) return TransitionResult.Busy;
                retainedGameplay = null;
                session = new Session(++generation, displayName);
                active = session;
            }
            var endpoint = $"{ipAddress}:{port}";
            PublishFor(session, IngameState.Connecting, IngameFailure.None, 0, 0, endpoint, false);
            try
            {
                using var deadline = CancellationTokenSource.CreateLinkedTokenSource(token, session.Cancellation.Token);
                deadline.CancelAfter(ConnectTimeout);
                var client = await SessionClient.ConnectAsync(ipAddress, port, SessionPassword, deadline.Token, displayName).ConfigureAwait(false);
                session.Client = client; // Own it before any later admission step can fail.
                BeforeResourceCommit?.Invoke();
                bool owned;
                bool cancelled;
                lock (gate)
                {
                    session.Client = client;
                    owned = ReferenceEquals(active, session);
                    cancelled = token.IsCancellationRequested;

                }
                if (!owned || cancelled)
                {
                    if (owned) await FinishFailureAsync(session, IngameFailure.None, endpoint, false).ConfigureAwait(false);
                    return TransitionResult.Superseded;
                }
                session.Pump = Task.Run(() => GuestPumpAsync(session, client));
                PublishFor(session, IngameState.Connected, IngameFailure.None, 1, 0, endpoint, false);
                return TransitionResult.Started;
            }
            catch (Exception ex)
            {
                var superseded = session.Abandoned;
                var sessionCancelled = session.Cancellation.IsCancellationRequested;
                var callerCancelled = token.IsCancellationRequested;
                if (superseded || callerCancelled) { await FinishFailureAsync(session, IngameFailure.None, endpoint, false).ConfigureAwait(false); return TransitionResult.Superseded; }
                var failure = ClassifyJoinFailure(ex, sessionCancelled, callerCancelled);
                if (failure == IngameFailure.None) { await FinishFailureAsync(session, failure, endpoint, false).ConfigureAwait(false); return TransitionResult.Superseded; }
                await FinishFailureAsync(session, failure, endpoint, false).ConfigureAwait(false);
                return TransitionResult.Failed;
            }
        }
        finally
        {
            transition.Release();
        }
    }

    /// <summary>
    /// Stops whichever session exists, including one whose join is still in flight.
    /// Never waits for the transition gate, so leaving during a connect attempt is immediate.
    /// </summary>
    public async Task LeaveAsync()
    {
        await leaveGate.WaitAsync().ConfigureAwait(false);
        try
        {
            Session? session;
            lock (gate)
            {
                session = pendingRelease ?? active;
                retainedGameplay?.Clear();
                retainedGameplay = null;
                session?.Gameplay.Clear();
                active = null;
                if (session is not null)
                {
                    pendingRelease = session;
                    session.Abandoned = true;
                    session.Cancellation.Cancel();
                }
                PublishDisconnected(IngameFailure.None);
            }
            if (session is null) return;
            await Task.Yield(); // socket teardown must not run on the native caller's thread
            await transition.WaitAsync().ConfigureAwait(false);
            try
            {
                try
                {
                    await ReleaseAsync(session).ConfigureAwait(false);
                    lock (gate) { if (ReferenceEquals(pendingRelease, session)) { pendingRelease = null; session.Cancellation.Dispose(); } }
                }
                catch
                {
                    lock (gate)
                        if (ReferenceEquals(pendingRelease, session)) Publish(IngameState.Failed, IngameFailure.Internal, 0, 0, "", false);
                    throw;
                }
            }
            finally { transition.Release(); }
        }
        finally
        {
            leaveGate.Release();
        }
    }

    /// <summary>Leaves and marks the controller unusable. The native library is never unloaded.</summary>
    public ValueTask DisposeAsync()
    {
        lock (gate)
        {
            Volatile.Write(ref disposed, 1);
            // Every caller observes the same teardown result, including a failure.
            return new ValueTask(disposal ??= LeaveAsync());
        }
    }

    private Task ReleaseAsync(Session session)
    {
        lock (session.ReleaseGate) return session.Release ??= ReleaseCoreAsync(session);
    }

    private async Task ReleaseCoreAsync(Session session)
    {
        session.Abandoned = true;
        session.Cancellation.Cancel();
        if (session.Host is { } host) await host.DisposeAsync().ConfigureAwait(false);
        if (session.Client is { } client) await client.DisposeAsync().ConfigureAwait(false);
        await session.Pump.ConfigureAwait(false);
        AfterResourcesReleased?.Invoke();
    }

    private void Forget(Session session)
    {
        lock (gate)
        {
            if (ReferenceEquals(active, session))
            {
                active = null;
                session.Abandoned = true;
            }
        }
    }

    private async Task FinishFailureAsync(Session session, IngameFailure failure, string endpoint, bool lan)
    {
        lock (gate)
        {
            if (!ReferenceEquals(active, session)) return; // Leave owns any superseded session.
            active = null;
            pendingRelease = session;
            session.Abandoned = true;
        }
        try { await ReleaseAsync(session).ConfigureAwait(false); }
        catch (Exception error)
        {
            System.Diagnostics.Trace.TraceError("In-game admission cleanup failed: {0}", error.GetType().Name);
            lock (gate)
                if (ReferenceEquals(pendingRelease, session)) Publish(IngameState.Failed, IngameFailure.Internal, 0, 0, endpoint, lan);
            return; // Keep the release owner; unknown resource state must not admit another session.
        }
        lock (gate)
        {
            if (!ReferenceEquals(pendingRelease, session)) return;
            pendingRelease = null;
            session.Cancellation.Dispose(); // Same gate as Leave cancellation and ownership clearing.
            if (failure == IngameFailure.None) PublishDisconnected(failure);
            else Publish(IngameState.Failed, failure, 0, 0, endpoint, lan);
        }
    }

    private void PublishFor(Session session, IngameState state, IngameFailure failure, int peers, int port, string endpoint, bool lan)
    {
        lock (gate)
            if (ReferenceEquals(active, session)) Publish(state, failure, peers, port, endpoint, lan);
    }

    private Task HostPumpAsync(Session session, SessionHost host) => HostGameplayPumpAsync(session, host);

    private void PublishHosting(Session session, SessionHost host)
    {
        lock (gate)
        {
            if (!ReferenceEquals(active, session)) return;
            var status = current;
            var lan = status.Endpoint.StartsWith("0.0.0.0:", StringComparison.Ordinal);
            Publish(IngameState.Hosting, IngameFailure.None, host.ConnectedClients, status.BoundPort, status.Endpoint, lan);
        }
    }

    private Task GuestPumpAsync(Session session, SessionClient client) => GuestGameplayPumpAsync(session, client);

    internal static IngameFailure ClassifyJoinFailure(Exception error, bool sessionCancelled, bool callerCancelled)
    {
        if (sessionCancelled || callerCancelled) return IngameFailure.None;
        return error switch
        {
            SocketException socket => socket.SocketErrorCode switch
            {
                SocketError.ConnectionRefused => IngameFailure.Refused,
                SocketError.TimedOut => IngameFailure.Timeout,
                SocketError.HostUnreachable or SocketError.NetworkUnreachable or SocketError.HostNotFound or SocketError.NoData
                    => IngameFailure.HostUnreachable,
                _ => IngameFailure.Internal,
            },
            EndOfStreamException => IngameFailure.HostFull,
            // A host at MaxClients disposes the accepted socket mid-handshake, which surfaces to
            // the client as IOException wrapping a connection reset (measured on Windows).
            IOException { InnerException: SocketException reset } when reset.SocketErrorCode is
                SocketError.ConnectionReset or SocketError.ConnectionAborted or SocketError.Shutdown => IngameFailure.HostFull,
            InvalidDataException => IngameFailure.Rejected,
            OperationCanceledException or TimeoutException => IngameFailure.Timeout,
            _ => IngameFailure.Internal,
        };
    }

    internal static bool TryParseIpv4(string? address, out IPAddress parsed)
    {
        parsed = IPAddress.None;
        if (string.IsNullOrEmpty(address) || address.Length > 15) return false;
        foreach (var c in address)
            if (c is not (>= '0' and <= '9' or '.')) return false;
        if (!IPAddress.TryParse(address, out var candidate) || candidate.AddressFamily != AddressFamily.InterNetwork) return false;
        parsed = candidate;
        return true;
    }

    private void PublishDisconnected(IngameFailure failure) => Publish(IngameState.Disconnected, failure, 0, 0, "", lan: false);

    private void Publish(IngameState state, IngameFailure failure, int peers, int boundPort, string endpoint, bool lan) =>
        Volatile.Write(ref current, new IngameStatus(state, failure, peers, boundPort, endpoint,
            IngameStatusText.Describe(state, failure, peers, boundPort, endpoint, lan)));
}
