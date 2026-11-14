using System.Net;
using System.Reflection;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Wsm.App;
using Wsm.Lobby;

internal static class LifecycleRegression
{
    public static async Task Run()
    {
        var failures = new List<Exception>();
        await Case("repeated close waits for cleanup", RepeatedClose);
        await Case("initial state failure rolls back join and permits retry", FailedInitialState);
        if (failures.Count != 0) throw new AggregateException(failures);
        async Task Case(string name, Func<Task> body)
        {
            try { await body(); Console.WriteLine("PASS " + name); }
            catch (Exception ex) { failures.Add(ex); Console.WriteLine("FAIL " + name + ": " + ex.Message); }
        }
    }

    private static T Control<T>(LobbyForm form, string name) where T : System.Windows.Forms.Control => (T)form.Controls.Find(name, true).Single();
    private static async Task Wait(Func<bool> condition)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(12));
        while (!condition()) await Task.Delay(25, timeout.Token);
    }

    private static async Task RepeatedClose()
    {
        var form = new LobbyForm(); form.Show();
        // Hold the real operation gate to make the asynchronous cleanup interval deterministic.
        var gate = (SemaphoreSlim)typeof(LobbyForm).GetField("gate", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(form)!;
        await gate.WaitAsync();
        try
        {
            form.Close();
            if (form.IsDisposed) throw new Exception("First close did not wait for the operation gate.");
            form.Close();
            if (form.IsDisposed) throw new Exception("Second close disposed the form before cleanup could finish.");
        }
        finally
        {
            gate.Release();
            await Task.Yield();
            await Wait(() => form.IsDisposed);
        }
    }

    private static async Task FailedInitialState()
    {
        await using var server = await LobbyServer.StartAsync(IPAddress.Loopback, 0, "LifecycleHost", new());
        using var forward = new HttpClient { BaseAddress = server.Address, Timeout = TimeSpan.FromSeconds(5) };
        var builder = WebApplication.CreateSlimBuilder();
        builder.Logging.ClearProviders();
        builder.WebHost.ConfigureKestrel(o => o.Listen(IPAddress.Loopback, 0));
        await using var proxy = builder.Build();
        int stateRequests = 0;
        proxy.Run(async context =>
        {
            if (context.Request.Method == "GET" && Interlocked.Increment(ref stateRequests) == 1)
            {
                context.Response.StatusCode = 503;
                return;
            }
            using var request = new HttpRequestMessage(new HttpMethod(context.Request.Method), context.Request.Path.Value);
            if (context.Request.ContentType is not null || context.Request.ContentLength > 0)
            {
                request.Content = new StreamContent(context.Request.Body);
                if (context.Request.ContentType is { } type) request.Content.Headers.TryAddWithoutValidation("Content-Type", type);
            }
            request.Headers.TryAddWithoutValidation("Authorization", context.Request.Headers.Authorization.ToArray());
            using var response = await forward.SendAsync(request, context.RequestAborted);
            context.Response.StatusCode = (int)response.StatusCode;
            context.Response.ContentType = response.Content.Headers.ContentType?.ToString();
            await response.Content.CopyToAsync(context.Response.Body, context.RequestAborted);
        });
        await proxy.StartAsync();
        var endpoint = new Uri(proxy.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.Single());
        var form = new LobbyForm(); form.Show();
        try
        {
            Control<NumericUpDown>(form, "Port").Value = endpoint.Port;
            Control<Button>(form, "Join").PerformClick();
            await Wait(() => Volatile.Read(ref stateRequests) != 0 && (Control<Button>(form, "Join").Enabled || Control<Button>(form, "Leave").Enabled));
            if (!Control<Button>(form, "Join").Enabled || server.Snapshot.Slots.Count != 1)
                throw new Exception("Failed initial state left an accepted join stranded instead of rolling it back.");
            Control<Button>(form, "Join").PerformClick();
            await Wait(() => Control<ListView>(form, "Roster").Items.Count == 2 && Control<Button>(form, "Leave").Enabled);
            if (server.Snapshot.Slots.Count != 2) throw new Exception("Retry left a duplicate guest slot.");
        }
        finally { form.Close(); await Wait(() => form.IsDisposed); }
    }
}
