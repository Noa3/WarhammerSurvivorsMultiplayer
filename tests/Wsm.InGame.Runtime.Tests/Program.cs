using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Text;
using Wsm.Core;
using Wsm.InGame.Runtime;

// Managed regression runner for the in-game session controller. Network scenarios
// use real loopback TCP; direct mailbox cases isolate deterministic ordering guards.

var failures = new List<string>();
// An optional substring argument selects scenarios, so one regression can be re-run without
// the full multi-minute suite. No argument keeps the complete suite.
var filter = args.Length > 0 ? args[0] : null;
var matched = 0;
foreach (var (name, test) in new (string, Func<Task>)[]
{
    ("player names roster over TCP", PlayerNamesTests.Roster),
    ("player names native ABI and frozen queue", PlayerNamesAbiTests.Run),
    ("host rules arrive in guest welcome", HostRulesTests.Welcome),
    ("observed bot decisions and personal offers", ObservedBotTests.Decisions),
    ("observed bot host-role and observation guards", ObservedBotTests.Guards),
    ("host rules cannot be changed by an admitted guest", HostRulesTests.GuestCannotEdit),
    ("LAN hint candidate filters", LanHintTests.Filters),
    ("LAN hint host ABI and guest admission", LanHintTests.Session),
    ("host and guest roundtrip with cleanup", HostAndGuestRoundtrip),
    ("mailbox reliable events survive continuous input", GameplayBridgeTests.MailboxFairness),
    ("idle reward polling avoids ledger allocation", GameplayBridgeTests.IdleRewardPollingAllocation),
    ("cancelled in-flight submission releases retained mailbox", GameplayBridgeTests.CancelledInflight),
    ("unpolled lifecycle burst remains available", GameplayBridgeTests.LifecycleBurst),
    ("preview host survives unpolled TCP churn", GameplayBridgeTests.PreviewHostChurn),
    ("terminal waits for reliable events", GameplayBridgeTests.TerminalAfterReliable),
    ("pre-join accepted upgrade survives independent reader failure", GameplayBridgeTests.PreJoinUpgradeSurvivesReaderFailure),
    ("pre-join missing provenance retains explicit failure", GameplayBridgeTests.PreJoinWithoutProvenanceRetainsFailure),
    ("each submission retains its correlated success or failure", GameplayBridgeTests.SubmissionResults),
    ("pending request ids are unique and reusable after completion", GameplayBridgeTests.DuplicatePendingRequest),
    ("gameplay input crosses real TCP into native polling mailbox", GameplayBridgeTests.InputRoundtrip),
    ("reported input follows its own join", GameplayBridgeTests.InputFollowsJoinUnderSlowLifecycleReader),
    ("reported join precedes leave for a quick disconnect", GameplayBridgeTests.JoinPrecedesLeaveUnderSlowLifecycleReader),
    ("gameplay world and recipient-private rewards and choices cross TCP", GameplayBridgeTests.WorldAndPersonalRewards),
    ("gameplay buffer refusal and session epoch isolation", GameplayBridgeTests.CapacityAndEpoch),
    ("gameplay malformed and unauthorized requests rejected with valid retry", GameplayBridgeTests.Validation),
    ("stalled consumer retains accepted rewards past channel capacity", GameplayBridgeTests.RewardsSurviveStalledPump),
    ("stalled bridge reconciles full ledger after host loss", GameplayBridgeTests.StalledBridgeReconcilesLedgerAfterHostLoss),
    ("gameplay unpolled reward burst retained across host loss", GameplayBridgeTests.RewardsSurviveUnpolledBurstAndHostLoss),
    ("join refused is classified and retry succeeds", JoinRefusedThenRetry),
    ("join deadline expires against a silent listener", JoinTimeout),
    ("full lobby is classified as hostfull", FullLobby),
    ("leave during connect supersedes the join", LeaveDuringJoin),
    ("leave during real host admission releases listener", LeaveDuringHostAdmission),
    ("fault after host binding releases listener and permits retry", FaultedHostAdmission),
    ("host request returns before blocked admission", HostRequestDoesNotBlockCaller),
    ("leave during client admission closes the admitted socket", LeaveDuringClientAdmission),
    ("queued native host cannot start after leave and preempt the next host", QueuedNativeStartAfterLeave),
    ("leave during completed release cannot cancel a disposed source", LeaveAtReleaseCompletion),
    ("dispose wins atomic host admission", DisposeDuringHostAdmission),
    ("dispose wins atomic client admission", DisposeDuringClientAdmission),
    ("host death moves the guest to hostlost", HostDeath),
    ("duplicate host keeps the running session", DuplicateHost),
    ("invalid input is rejected", InvalidInput),
    ("status text stays bounded and German", StatusTextContract),
})
{
    if (filter is not null && !name.Contains(filter, StringComparison.OrdinalIgnoreCase)) continue;
    matched++;
    try { await test(); Console.WriteLine("PASS " + name); }
    catch (Exception ex) { failures.Add(name + ": " + ex.Message); Console.WriteLine("FAIL " + name + " -> " + ex.Message); }
}
if (matched == 0) throw new Exception($"No scenarios matched filter: {filter}");
if (failures.Count != 0) throw new Exception($"{failures.Count} scenario(s) failed: {string.Join(" | ", failures)}");
Console.WriteLine("PASS ingame runtime controller suite");

static async Task HostAndGuestRoundtrip()
{
    await using var host = new IngameSessionController();
    await using var guest = new IngameSessionController();
    Check(await host.HostAsync(0, bindToLan: false) == TransitionResult.Started, "host did not start");
    Check(host.Status.State == IngameState.Hosting, "host state");
    Check(host.Status.BoundPort > 0, "ephemeral port must be reported");
    var port = host.Status.BoundPort;

    Check(await guest.JoinAsync("127.0.0.1", port) == TransitionResult.Started, "guest join did not start");
    Check(guest.Status.State == IngameState.Connected, "guest state");
    Check(guest.Status.Endpoint == $"127.0.0.1:{port}", "guest endpoint");
    await WaitFor(() => host.Status.PeerCount == 1, "host peer count after join");

    await guest.LeaveAsync();
    Check(guest.Status.State == IngameState.Disconnected, "guest left");
    Check(guest.Status.PeerCount == 0, "guest peer count after leave");
    await WaitFor(() => host.Status.PeerCount == 0, "host peer count after guest leave");

    await host.LeaveAsync();
    Check(host.Status.State == IngameState.Disconnected, "host left");
    await RequireRefused(port, "listener still bound after host leave");
}

static async Task JoinRefusedThenRetry()
{
    var deadPort = ReserveFreePort();
    await using var guest = new IngameSessionController();
    var result = await guest.JoinAsync("127.0.0.1", deadPort);
    Check(result == TransitionResult.Failed, $"expected failure, got {result}");
    Check(guest.Status.State == IngameState.Failed, "failed state");
    Check(guest.Status.Failure == IngameFailure.Refused, $"expected refused, got {guest.Status.Failure}");
    Check(guest.Status.FailureCode == "refused", "failure code");
    Check(guest.Status.Text.Contains("abgelehnt", StringComparison.Ordinal), "German refusal text");

    await using var host = new IngameSessionController();
    Check(await host.HostAsync(0, bindToLan: false) == TransitionResult.Started, "retry host");
    Check(await guest.JoinAsync("127.0.0.1", host.Status.BoundPort) == TransitionResult.Started, "retry join after failure");
    Check(guest.Status.State == IngameState.Connected, "retry connected");
    Check(guest.Status.Failure == IngameFailure.None, "failure cleared on success");
}

static async Task JoinTimeout()
{
    var listener = new TcpListener(IPAddress.Loopback, 0);
    listener.Start();
    var port = ((IPEndPoint)listener.LocalEndpoint).Port;
    var held = new List<TcpClient>();
    var accepted = Task.Run(async () => { try { held.Add(await listener.AcceptTcpClientAsync()); } catch (Exception) { } });
    try
    {
        await using var guest = new IngameSessionController();
        var watch = Stopwatch.StartNew();
        var result = await guest.JoinAsync("127.0.0.1", port);
        watch.Stop();
        Check(result == TransitionResult.Failed, $"expected failure, got {result}");
        Check(guest.Status.Failure == IngameFailure.Timeout, $"expected timeout, got {guest.Status.Failure}");
        Check(watch.Elapsed >= TimeSpan.FromSeconds(6) && watch.Elapsed < TimeSpan.FromSeconds(15),
            $"deadline not honoured: {watch.Elapsed.TotalSeconds:0.0}s");
        Check(guest.Status.Text.Contains("Zeitüberschreitung", StringComparison.Ordinal), "German timeout text");
    }
    finally
    {
        listener.Stop();
        foreach (var client in held) client.Dispose();
        await accepted;
    }
}

static async Task FullLobby()
{
    await using var host = new IngameSessionController();
    Check(await host.HostAsync(0, bindToLan: false) == TransitionResult.Started, "host start");
    var port = host.Status.BoundPort;
    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(15));
    var guests = new List<SessionClient>();
    try
    {
        for (var i = 0; i < IngameSessionController.MaxClients; i++)
            guests.Add(await SessionClient.ConnectAsync(IPAddress.Loopback, port, "", timeout.Token));
        await WaitFor(() => host.Status.PeerCount == IngameSessionController.MaxClients, "host must see all guests");

        await using var extra = new IngameSessionController();
        var result = await extra.JoinAsync("127.0.0.1", port);
        Check(result == TransitionResult.Failed, $"expected rejection, got {result}");
        Check(extra.Status.Failure == IngameFailure.HostFull, $"expected hostfull, got {extra.Status.Failure}");
    }
    finally
    {
        foreach (var guest in guests) await guest.DisposeAsync();
    }
}

static async Task LeaveDuringJoin()
{
    var listener = new TcpListener(IPAddress.Loopback, 0);
    listener.Start();
    var port = ((IPEndPoint)listener.LocalEndpoint).Port;
    var held = new List<TcpClient>();
    var accepted = Task.Run(async () => { try { held.Add(await listener.AcceptTcpClientAsync()); } catch (Exception) { } });
    try
    {
        var guest = new IngameSessionController();
        var watch = Stopwatch.StartNew();
        var join = guest.JoinAsync("127.0.0.1", port);
        await Task.Delay(200);
        await guest.LeaveAsync();
        var result = await join;
        watch.Stop();
        Check(result == TransitionResult.Superseded, $"expected superseded, got {result}");
        Check(watch.Elapsed < TimeSpan.FromSeconds(4), $"leave did not interrupt the join: {watch.Elapsed.TotalSeconds:0.0}s");
        Check(guest.Status.State == IngameState.Disconnected, "guest state after interrupted join");
        Check(guest.Status.Failure == IngameFailure.None, "leave must not report a failure");
        await guest.DisposeAsync();
    }
    finally
    {
        listener.Stop();
        foreach (var client in held) client.Dispose();
        await accepted;
    }
}

static async Task LeaveDuringHostAdmission()
{
    var port = ReserveFreePort();
    using var entered = new ManualResetEventSlim();
    using var resume = new ManualResetEventSlim();
    await using var controller = new IngameSessionController();
    controller.BeforeResourceCommit = () => { entered.Set(); if (!resume.Wait(TimeSpan.FromSeconds(5))) throw new TimeoutException("admission gate"); };
    var start = Task.Run(() => controller.HostAsync(port, false));
    try
    {
        Check(entered.Wait(TimeSpan.FromSeconds(5)), "host did not reach resource admission");
        var leave = controller.LeaveAsync();
        Check(controller.Status.State == IngameState.Disconnected, "leave must publish disconnected immediately");
        resume.Set();
        Check(await start == TransitionResult.Superseded, "host must be superseded");
        await leave.WaitAsync(TimeSpan.FromSeconds(5));
        await RequireRefused(port, "superseded listener still bound");
        Check(controller.Status.State == IngameState.Disconnected, "late host must not publish");
        controller.BeforeResourceCommit = null;
        Check(await controller.HostAsync(0, false) == TransitionResult.Started, "host retry after leave");
    }
    finally { resume.Set(); }
}

static async Task FaultedHostAdmission()
{
    var port = ReserveFreePort();
    await using var controller = new IngameSessionController();
    controller.BeforeResourceCommit = () => throw new InvalidOperationException("controlled admission failure");
    Check(await controller.HostAsync(port, false) == TransitionResult.Failed, "controlled failure must be reported");
    await RequireRefused(port, "failed host admission leaked its listening socket");
    controller.BeforeResourceCommit = null;
    Check(await controller.HostAsync(0, false) == TransitionResult.Started, "retry after failed admission");
}

static async Task HostRequestDoesNotBlockCaller()
{
    using var entered = new ManualResetEventSlim();
    using var resume = new ManualResetEventSlim();
    using var returned = new ManualResetEventSlim();
    await using var controller = new IngameSessionController();
    controller.BeforeResourceCommit = () => { entered.Set(); if (!resume.Wait(TimeSpan.FromSeconds(5))) throw new TimeoutException("bounded admission gate"); };
    Task<TransitionResult>? operation = null;
    var caller = new Thread(() => { operation = controller.HostAsync(0, false); returned.Set(); });
    caller.Start();
    bool responsive;
    try
    {
        Check(entered.Wait(TimeSpan.FromSeconds(5)), "real host did not reach the controlled gate");
        responsive = returned.Wait(TimeSpan.FromMilliseconds(250));
    }
    finally { resume.Set(); caller.Join(TimeSpan.FromSeconds(5)); }
    if (operation is not null) await operation;
    Check(responsive, "HostAsync blocked its caller while binding/admitting the real host");
}

static async Task LeaveDuringClientAdmission()
{
    using var entered = new ManualResetEventSlim();
    using var resume = new ManualResetEventSlim();
    await using var host = new IngameSessionController();
    await using var guest = new IngameSessionController();
    Check(await host.HostAsync(0, false) == TransitionResult.Started, "admission host start");
    guest.BeforeResourceCommit = () => { entered.Set(); if (!resume.Wait(TimeSpan.FromSeconds(5))) throw new TimeoutException("client admission gate"); };
    var join = Task.Run(() => guest.JoinAsync("127.0.0.1", host.Status.BoundPort));
    try
    {
        Check(entered.Wait(TimeSpan.FromSeconds(5)), "client did not reach admission");
        var leave = guest.LeaveAsync();
        resume.Set();
        Check(await join == TransitionResult.Superseded, "late client must be superseded");
        await leave.WaitAsync(TimeSpan.FromSeconds(5));
        await WaitFor(() => host.Status.PeerCount == 0, "superseded guest socket still admitted");
        Check(guest.Status.State == IngameState.Disconnected, "late client must not republish connected");
    }
    finally { resume.Set(); }
}

static unsafe int NativeHost(int port) { delegate* unmanaged[Cdecl]<int,int,int> call = &NativeExports.Host; return call(port,0); }
static unsafe int NativeLeave() { delegate* unmanaged[Cdecl]<int> call = &NativeExports.Leave; return call(); }

static async Task QueuedNativeStartAfterLeave()
{
    using var entered = new ManualResetEventSlim();
    using var resume = new ManualResetEventSlim();
    var controller = NativeExports.ControllerForTests;
    var oldPort = ReserveFreePort();
    var newPort = ReserveFreePort();
    while (newPort == oldPort) newPort = ReserveFreePort();
    controller.BeforeOperationStart = () => { entered.Set(); if (!resume.Wait(TimeSpan.FromSeconds(5))) throw new TimeoutException("queued start gate"); };
    try
    {
        Check(NativeHost(oldPort) == 0, "old request not queued");
        Check(entered.Wait(TimeSpan.FromSeconds(5)), "native worker did not reach queued-start gate");
        var watch = Stopwatch.StartNew();
        Check(NativeLeave() == 0, "native leave rejected");
        Check(watch.Elapsed < TimeSpan.FromMilliseconds(250), "native leave blocked its caller");
        controller.BeforeOperationStart = null;
        Check(NativeHost(newPort) == 0, "fresh request not queued");
        resume.Set();
        await WaitFor(() => controller.Status.State == IngameState.Hosting, "fresh host completion");
        Check(controller.Status.BoundPort == newPort, "old queued host resurrected and preempted the fresh request");
        await RequireRefused(oldPort, "superseded old request left a listener");
    }
    finally
    {
        controller.BeforeOperationStart = null; resume.Set(); NativeLeave();
        await WaitFor(() => controller.Status.State == IngameState.Disconnected, "native leave completion");
    }
}

static async Task LeaveAtReleaseCompletion()
{
    using var entered = new ManualResetEventSlim();
    using var resume = new ManualResetEventSlim();
    var controller = new IngameSessionController();
    controller.BeforeResourceCommit = () => throw new InvalidOperationException("controlled start failure");
    controller.AfterResourcesReleased = () => { entered.Set(); if (!resume.Wait(TimeSpan.FromSeconds(5))) throw new TimeoutException("release completion gate"); };
    var start = controller.HostAsync(0, false);
    try
    {
        Check(entered.Wait(TimeSpan.FromSeconds(5)), "release did not reach ownership handoff");
        var leave = controller.LeaveAsync();
        var failedBeforeHandoff = leave.IsFaulted;
        resume.Set();
        Exception? departureError = null;
        try { await leave.WaitAsync(TimeSpan.FromSeconds(5)); } catch (Exception error) { departureError = error; }
        await start;
        Check(!failedBeforeHandoff && departureError is null, "leave raced cancellation-source disposal: " + departureError?.GetType().Name);
        controller.BeforeResourceCommit = null; controller.AfterResourcesReleased = null;
        Check(await controller.HostAsync(0, false) == TransitionResult.Started, "fresh host after release/leave race");
    }
    finally
    {
        resume.Set(); controller.BeforeResourceCommit = null; controller.AfterResourcesReleased = null;
        await start; await controller.LeaveAsync(); await controller.DisposeAsync();
    }
}

static Task DisposeDuringHostAdmission() => DisposeDuringAdmission(false);
static Task DisposeDuringClientAdmission() => DisposeDuringAdmission(true);
static async Task DisposeDuringAdmission(bool joining)
{
    using var entered = new ManualResetEventSlim();
    using var resume = new ManualResetEventSlim();
    await using var remote = new IngameSessionController();
    if (joining) Check(await remote.HostAsync(0, false) == TransitionResult.Started, "remote host start");
    var controller = new IngameSessionController();
    controller.BeforeSessionAdmission = () => { entered.Set(); if (!resume.Wait(TimeSpan.FromSeconds(5))) throw new TimeoutException("atomic admission gate"); };
    var operation = joining ? controller.JoinAsync("127.0.0.1", remote.Status.BoundPort) : controller.HostAsync(0, false);
    try
    {
        Check(entered.Wait(TimeSpan.FromSeconds(5)), "start did not reach admission gap");
        await controller.DisposeAsync();
        resume.Set();
        var result = await operation;
        Check(result != TransitionResult.Started, "session installed after controller disposal completed");
        Check(controller.Status.State == IngameState.Disconnected && controller.Status.BoundPort == 0, "disposed controller published a live session");
        if (joining) await WaitFor(() => remote.Status.PeerCount == 0, "disposed guest left an admitted socket");
    }
    finally { resume.Set(); await operation; controller.BeforeSessionAdmission = null; await controller.LeaveAsync(); }
}

static async Task HostDeath()
{
    await using var host = new IngameSessionController();
    await using var guest = new IngameSessionController();
    Check(await host.HostAsync(0, bindToLan: false) == TransitionResult.Started, "host start");
    Check(await guest.JoinAsync("127.0.0.1", host.Status.BoundPort) == TransitionResult.Started, "guest join");
    await WaitFor(() => host.Status.PeerCount == 1, "host sees guest");

    await host.LeaveAsync();
    await WaitFor(() => guest.Status.State == IngameState.Disconnected, "guest must notice host death");
    Check(guest.Status.Failure == IngameFailure.HostLost, $"expected hostlost, got {guest.Status.Failure}");
    Check(guest.Status.FailureCode == "hostlost", "hostlost code");
    Check(guest.Status.Text.Contains("Verbindung zum Host verloren", StringComparison.Ordinal), "German hostlost text");
}

static async Task DuplicateHost()
{
    await using var host = new IngameSessionController();
    Check(await host.HostAsync(0, bindToLan: false) == TransitionResult.Started, "first host");
    var port = host.Status.BoundPort;
    Check(await host.HostAsync(0, bindToLan: false) == TransitionResult.Busy, "second host must be busy");
    Check(host.Status.State == IngameState.Hosting, "busy host keeps hosting");
    Check(host.Status.BoundPort == port, "busy host keeps its port");
    // The first session still accepts real guests.
    await using var guest = new IngameSessionController();
    Check(await guest.JoinAsync("127.0.0.1", port) == TransitionResult.Started, "guest still joins after busy host call");
}

static async Task InvalidInput()
{
    await using var controller = new IngameSessionController();
    Check(await controller.JoinAsync("host.local", 7780) == TransitionResult.InvalidArgument, "DNS name must be rejected");
    Check(await controller.JoinAsync("999.1.1.1", 7780) == TransitionResult.InvalidArgument, "invalid IPv4 must be rejected");
    Check(await controller.JoinAsync("127.0.0.1", 0) == TransitionResult.InvalidArgument, "port 0 must be rejected for join");
    Check(await controller.JoinAsync("127.0.0.1", 70000) == TransitionResult.InvalidArgument, "port out of range");
    Check(await controller.HostAsync(70000, bindToLan: false) == TransitionResult.InvalidArgument, "host port out of range");
    Check(controller.Status.State == IngameState.Disconnected, "invalid input must not change state");
}

static Task StatusTextContract()
{
    var samples = new[]
    {
        IngameStatusText.Describe(IngameState.Hosting, IngameFailure.None, 2, 7780, "0.0.0.0:7780", true),
        IngameStatusText.Describe(IngameState.Failed, IngameFailure.Refused, 0, 0, "127.0.0.1:7780", false),
        IngameStatusText.Describe(IngameState.Disconnected, IngameFailure.HostLost, 0, 0, "", false),
    };
    foreach (var text in samples)
    {
        var bytes = IngameStatusText.ToBoundedUtf8(text);
        Check(bytes.Length is > 0 and <= IngameStatusText.MaxBytes - 1, "status text must stay bounded");
        Check(Encoding.UTF8.GetString(bytes).Length > 0, "status text must decode");
    }
    Check(IngameStatusText.ToBoundedUtf8(IngameStatusText.Describe(IngameState.Hosting, IngameFailure.None, 0, 7780, "0.0.0.0:7780", true)).Length <= 4095, "4096 byte cap");
    var stupid = IngameStatusText.ToBoundedUtf8(new string('ä', 8000));
    Check(stupid.Length <= IngameStatusText.MaxBytes - 1, "cap applies to multi-byte text");
    Check(Encoding.UTF8.GetString(stupid).All(c => c == 'ä'), "truncation must not split UTF-8 sequences");
    return Task.CompletedTask;
}

static int ReserveFreePort()
{
    var listener = new TcpListener(IPAddress.Loopback, 0);
    listener.Start();
    var port = ((IPEndPoint)listener.LocalEndpoint).Port;
    listener.Stop();
    return port;
}

static async Task RequireRefused(int port, string message)
{
    using var client = new TcpClient();
    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
    try
    {
        await client.ConnectAsync(IPAddress.Loopback, port, timeout.Token);
        throw new Exception(message);
    }
    catch (SocketException ex) when (ex.SocketErrorCode == SocketError.ConnectionRefused) { }
}

static async Task WaitFor(Func<bool> condition, string message)
{
    var watch = Stopwatch.StartNew();
    while (!condition())
    {
        if (watch.Elapsed > TimeSpan.FromSeconds(10)) throw new Exception("timeout: " + message);
        await Task.Delay(20);
    }
}

static void Check(bool value, string message) { if (!value) throw new Exception(message); }
