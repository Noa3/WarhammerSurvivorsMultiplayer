using System.Net;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Wsm.Lobby;

internal static class LeaveCleanupCases
{
    private static void Check(bool condition, string message) { if (!condition) throw new Exception(message); }

    public static async Task FailedLeavesKeepAlive()
    {
        await using var server = await LobbyServer.StartAsync(IPAddress.Loopback, 0, "host", new());
        var builder = WebApplication.CreateSlimBuilder(); builder.Logging.ClearProviders();
        builder.WebHost.ConfigureKestrel(o => o.Listen(IPAddress.Loopback, 0));
        await using var proxy = builder.Build();
        using var upstream = new HttpClient { BaseAddress = server.Address };
        int leaves = 0;
        string? canceledAuthorization = null;
        var canceledLeaveReceived = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var heartbeatDuringLeave = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        proxy.Run(async ctx =>
        {
            if (ctx.Request.Path == "/heartbeat" && canceledLeaveReceived.Task.IsCompleted &&
                ctx.Request.Headers.Authorization.ToString() == Volatile.Read(ref canceledAuthorization)) heartbeatDuringLeave.TrySetResult();
            int attempt = ctx.Request.Path == "/leave" ? Interlocked.Increment(ref leaves) : 0;
            if (attempt != 0)
            {
                if (attempt == 1) { ctx.Response.StatusCode = 503; return; }
                if (attempt == 2)
                {
                    Volatile.Write(ref canceledAuthorization, ctx.Request.Headers.Authorization.ToString());
                    canceledLeaveReceived.TrySetResult();
                    try { await Task.Delay(Timeout.InfiniteTimeSpan, ctx.RequestAborted); }
                    catch (OperationCanceledException) { }
                    return;
                }
            }
            using var request = new HttpRequestMessage(new HttpMethod(ctx.Request.Method), ctx.Request.Path.Value);
            if (ctx.Request.Headers.Authorization.Count != 0)
                request.Headers.TryAddWithoutValidation("Authorization", ctx.Request.Headers.Authorization.ToArray());
            if (ctx.Request.Method == "POST")
            {
                request.Content = new StreamContent(ctx.Request.Body);
                if (ctx.Request.ContentType is not null) request.Content.Headers.TryAddWithoutValidation("Content-Type", ctx.Request.ContentType);
            }
            using var response = await upstream.SendAsync(request, ctx.RequestAborted);
            // Simulate a gateway failure after the server has already applied leave.
            if (attempt == 3) { ctx.Response.StatusCode = 503; return; }
            ctx.Response.StatusCode = (int)response.StatusCode;
            if (response.Content.Headers.ContentType is not null) ctx.Response.ContentType = response.Content.Headers.ContentType.ToString();
            await response.Content.CopyToAsync(ctx.Response.Body, ctx.RequestAborted);
        });
        await proxy.StartAsync();
        var address = new Uri(proxy.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.Single());
        var failed = await LobbyClient.JoinAsync(address, "failed");
        var canceled = await LobbyClient.JoinAsync(address, "canceled");
        try
        {
            try { await failed.LeaveAsync(); throw new Exception("Expected a 503 leave failure."); }
            catch (HttpRequestException ex) when (ex.StatusCode == HttpStatusCode.ServiceUnavailable) { }
            using var cts = new CancellationTokenSource();
            var leaving = canceled.LeaveAsync(cts.Token);
            try
            {
                await canceledLeaveReceived.Task.WaitAsync(TimeSpan.FromSeconds(5));
                await heartbeatDuringLeave.Task.WaitAsync(TimeSpan.FromSeconds(5));
            }
            finally { cts.Cancel(); }
            try { await leaving; throw new Exception("Expected canceled leave."); }
            catch (OperationCanceledException) { }
            // No state reads or manual heartbeats during the full expiry interval.
            await Task.Delay(TimeSpan.FromSeconds(19));
            var snapshot = server.Snapshot;
            Check(snapshot.Slots.Any(s => s.PlayerId == failed.PlayerId), "503 leave must preserve automatic heartbeat and the guest beyond expiry.");
            Check(snapshot.Slots.Any(s => s.PlayerId == canceled.PlayerId), "Canceled leave must preserve automatic heartbeat and the guest beyond expiry.");
            try { await failed.LeaveAsync(); throw new Exception("Expected lost acknowledgement failure."); }
            catch (HttpRequestException ex) when (ex.StatusCode == HttpStatusCode.ServiceUnavailable) { }
            Check(server.Snapshot.Slots.All(s => s.PlayerId != failed.PlayerId), "A failed response must not imply the server failed to apply leave.");
            await failed.LeaveAsync(); // The server's 401 confirms the session is already gone.
            await canceled.LeaveAsync();
            Check(server.Snapshot.Slots.Count == 1, "Successful retry must remove both guests.");
        }
        finally { await failed.DisposeAsync(); await canceled.DisposeAsync(); }
        Console.WriteLine("PASS failed and canceled leave retain independent heartbeat, retry, disposal");
    }

    public static async Task FaultedHeartbeatDisposesHttp()
    {
        foreach (bool failLeave in new[] { true, false })
        {
            var builder = WebApplication.CreateSlimBuilder(); builder.Logging.ClearProviders();
            builder.WebHost.ConfigureKestrel(o => o.Listen(IPAddress.Loopback, 0));
            await using var fake = builder.Build();
            fake.MapPost("/join", () => Results.Json(new { playerId = Guid.NewGuid(), token = new string('A', 64), protocolVersion = LobbyProtocol.Version }));
            fake.MapPost("/leave", () => failLeave ? Results.StatusCode(503) : Results.NoContent());
            fake.MapPost("/heartbeat", () => Results.NoContent());
            await fake.StartAsync();
            var address = new Uri(fake.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.Single());
            var client = await LobbyClient.JoinAsync(address, "fault-injection");
            var heartbeatField = typeof(LobbyClient).GetField("heartbeat", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!;
            var original = (Task)heartbeatField.GetValue(client)!;
            var failure = new InvalidOperationException("injected heartbeat failure");
            heartbeatField.SetValue(client, Task.FromException(failure));
            Exception? observed = null;
            try { await client.DisposeAsync(); } catch (Exception ex) { observed = ex; }
            await original.WaitAsync(TimeSpan.FromSeconds(5));
            Check(failLeave ? observed is HttpRequestException { StatusCode: HttpStatusCode.ServiceUnavailable } : ReferenceEquals(observed, failure),
                "Dispose must preserve the primary leave failure, or expose the heartbeat failure if leave succeeded.");
            var http = (HttpClient)typeof(LobbyClient).GetField("http", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!.GetValue(client)!;
            try { await http.GetAsync("/state"); throw new Exception("HTTP client was not disposed after heartbeat fault."); }
            catch (ObjectDisposedException) { }
        }
        Console.WriteLine("PASS heartbeat fault still disposes HTTP and preserves primary failure");
    }

    public static async Task LeaveDuringLaunch()
    {
        foreach (int outcome in new[] { 1, 0, 2 })
        {
            bool succeeds = outcome == 1;
            var coordinator = new BlockingCoordinator(outcome);
            await using var server = await LobbyServer.StartAsync(IPAddress.Loopback, 0, "host", new(), coordinator);
            var guest = await LobbyClient.JoinAsync(server.Address, "guest");
            await using var remaining = await LobbyClient.JoinAsync(server.Address, "remaining");
            await using var second = await LobbyClient.JoinAsync(server.Address, "second-departure");
            await guest.SetLoadoutAsync("hero", ["weapon"]); await guest.SetReadyAsync(true);
            await remaining.SetLoadoutAsync("other", ["weapon"]); await remaining.SetReadyAsync(true);
            await second.SetLoadoutAsync("second", ["weapon"]); await second.SetReadyAsync(true);
            server.SetHostLoadout("host", ["weapon"]); server.SetHostReady(true);
            long before = server.Snapshot.Version;
            var launch = Task.Run(server.TryStart);
            try
            {
                await coordinator.Entered.Task.WaitAsync(TimeSpan.FromSeconds(5));
                await Task.WhenAll(guest.DisposeAsync().AsTask(), second.LeaveAsync()).WaitAsync(TimeSpan.FromSeconds(5));
                try { await second.HeartbeatAsync(); throw new Exception("Queued departure token still accepted."); }
                catch (HttpRequestException ex) when (ex.StatusCode == HttpStatusCode.Unauthorized) { }
                await remaining.HeartbeatAsync().WaitAsync(TimeSpan.FromSeconds(5));
                Check(coordinator.Snapshot!.Slots.Any(s => s.PlayerId == guest.PlayerId), "Coordinator snapshot must retain launch participants.");
                coordinator.Release.TrySetResult();
                if (outcome == 2)
                {
                    try { await launch.WaitAsync(TimeSpan.FromSeconds(5)); throw new Exception("Expected coordinator failure."); }
                    catch (InvalidOperationException ex) when (ex.Message == "fixture failure") { }
                }
                else
                {
                    var result = await launch.WaitAsync(TimeSpan.FromSeconds(5));
                    Check(result.Started == succeeds, "Start result must reflect coordinator result.");
                }
                var snapshot = server.Snapshot;
                Check(snapshot.Started == succeeds, "Control-plane start must reflect coordinator result.");
                Check(snapshot.Slots.All(s => s.PlayerId != guest.PlayerId && s.PlayerId != second.PlayerId), "All queued departing guests must be removed after launch completion.");
                Check(snapshot.Version == before + (succeeds ? 2 : 1) && snapshot.Slots.Where(s => s.Kind != SlotKind.Bot).All(s => !s.Ready), "Departure must advance version and reset readiness independently of start.");
                Check(coordinator.Snapshot.Slots.Any(s => s.PlayerId == guest.PlayerId), "Launch snapshot must remain unchanged after departure.");
            }
            finally { coordinator.Release.TrySetResult(); try { await launch; } catch { } try { await guest.DisposeAsync(); } catch { } }
        }
        Console.WriteLine("PASS concurrent departures revoke tokens and remove guests after launch success, refusal, and exception");
    }

    private sealed class BlockingCoordinator(int outcome) : IGameLaunchCoordinator
    {
        public TaskCompletionSource Entered { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
        public TaskCompletionSource Release { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
        public LobbySnapshot? Snapshot { get; private set; }
        public StartResult TryLaunch(LobbySnapshot snapshot)
        {
            Snapshot = snapshot; Entered.TrySetResult();
            if (!Release.Task.Wait(TimeSpan.FromSeconds(15))) throw new TimeoutException("Fixture launch was not released.");
            if (outcome == 2) throw new InvalidOperationException("fixture failure");
            return new(outcome == 1, "fixture only");
        }
    }
}
