using System.Collections.Concurrent;
using System.Net;
using Wsm.Core;

internal static class BlockedContextRegression
{
    private sealed class MenuContext : SynchronizationContext
    {
        private readonly BlockingCollection<(SendOrPostCallback Callback, object? State)> work = new();
        public override void Post(SendOrPostCallback callback, object? state) => work.Add((callback, state));
        public void Pump() { if (work.TryTake(out var next, 50)) next.Callback(next.State); }
    }

    public static async Task Run()
    {
        using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(50));
        await using var host = new SessionHost(new(1, DeathMode.AllDead, 0, BotDifficulty.Normal), "context-fixture");
        host.Start();
        using var release = new ManualResetEventSlim();
        var admitted = new TaskCompletionSource<SessionClient>(TaskCreationOptions.RunContinuationsAsynchronously);
        var stopped = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var menuThread = new Thread(() =>
        {
            var context = new MenuContext(); SynchronizationContext.SetSynchronizationContext(context);
            SessionClient? client = null;
            try
            {
                var connecting = SessionClient.ConnectAsync(IPAddress.Loopback, host.Port, "context-fixture", deadline.Token);
                while (!connecting.IsCompleted) { deadline.Token.ThrowIfCancellationRequested(); context.Pump(); }
                client = connecting.GetAwaiter().GetResult(); admitted.SetResult(client);
                // Deliberately do not pump the UI/game context, like a stopped menu update loop.
                if (!release.Wait(TimeSpan.FromSeconds(40))) throw new TimeoutException("Menu release missing.");
                var disposing = client.DisposeAsync().AsTask();
                while (!disposing.IsCompleted) { deadline.Token.ThrowIfCancellationRequested(); context.Pump(); }
                disposing.GetAwaiter().GetResult(); stopped.SetResult();
            }
            catch (Exception error) { admitted.TrySetException(error); stopped.TrySetException(error); }
            finally { SynchronizationContext.SetSynchronizationContext(null); }
        }) { IsBackground = true, Name = "WSM blocked menu context fixture" };
        menuThread.Start();
        try
        {
            var client = await admitted.Task.WaitAsync(deadline.Token);
            await host.Joined.ReadAsync(deadline.Token);
            await Task.Delay(TimeSpan.FromSeconds(32), deadline.Token);
            if (host.ConnectedClients != 1 || client.Rewards.Completion.IsCompleted)
                throw new Exception("TCP heartbeat/receive loop depends on a blocked UI/game synchronization context.");
            await host.PublishSnapshotAsync(new(1, []), deadline.Token);
            if ((await client.Snapshots.ReadAsync(deadline.Token)).Tick != 1)
                throw new Exception("Network receive did not progress while menu context was blocked.");
            Console.WriteLine("PASS TCP liveness and snapshot delivery with UI/game context blocked >30s");
        }
        finally
        {
            release.Set();
            await stopped.Task.WaitAsync(deadline.Token);
            if (!menuThread.Join(2000)) throw new Exception("Owned menu thread did not stop.");
        }
    }
}
