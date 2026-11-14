using System.Net;
using System.Net.Sockets;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Wsm.App;
using Wsm.Lobby;

internal static class FeedbackRegression
{
    private static T C<T>(LobbyForm f, string name) where T : Control => (T)f.Controls.Find(name, true).Single();
    private static async Task CheckJoin(int port, string expected)
    {
        var form = new LobbyForm(); form.Show();
        try
        {
            C<NumericUpDown>(form, "Port").Value = port;
            C<Button>(form, "Join").PerformClick();
            using var limit = new CancellationTokenSource(TimeSpan.FromSeconds(15));
            while (!C<Button>(form, "Join").Enabled) await Task.Delay(25, limit.Token);
            var status = C<Label>(form, "Status").Text;
            if (!status.Contains(expected, StringComparison.Ordinal) || !status.Contains($"127.0.0.1:{port}", StringComparison.Ordinal))
                throw new Exception($"Expected actionable feedback '{expected}' and attempted endpoint; got '{status}'.");
            if (C<Button>(form, "Ready").Enabled || C<ListView>(form, "Roster").Items.Count != 0)
                throw new Exception("A failed connection must not advertise a live session.");
        }
        finally { form.Close(); while (!form.IsDisposed) await Task.Delay(25); }
    }
    public static async Task Run()
    {
        var unused = new TcpListener(IPAddress.Loopback, 0); unused.Start();
        var closedPort = ((IPEndPoint)unused.LocalEndpoint).Port; unused.Stop();
        await CheckJoin(closedPort, "Keine Lobby erreichbar");
        await using var full = await LobbyServer.StartAsync(IPAddress.Loopback, 0, "FeedbackFull", new(3));
        await CheckJoin(full.Address.Port, "Lobby ist voll");
        var builder = WebApplication.CreateSlimBuilder(); builder.Logging.ClearProviders();
        builder.WebHost.ConfigureKestrel(o => o.Listen(IPAddress.Loopback, 0));
        await using var slow = builder.Build();
        slow.MapPost("/join", async (HttpContext context) => { await Task.Delay(TimeSpan.FromSeconds(30), context.RequestAborted); });
        await slow.StartAsync();
        var address = new Uri(slow.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.Single());
        await CheckJoin(address.Port, "Zeitüberschreitung");
        Console.WriteLine("PASS GUI connection feedback: real refused port, full lobby, timeout, endpoint and retry controls");
    }
}
