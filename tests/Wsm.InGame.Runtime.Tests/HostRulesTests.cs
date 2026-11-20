using System.Net;
using Wsm.Core;
using Wsm.InGame.Runtime;

internal static class HostRulesTests
{
    private static unsafe int Start(int bots, int difficulty, int death)
    {
        delegate* unmanaged[Cdecl]<int, int, int, int, int, int> host = &NativeExports.HostRules;
        return host(0, 0, bots, difficulty, death);
    }

    public static async Task Welcome()
    {
        var controller = NativeExports.ControllerForTests;
        try
        {
            foreach (var (bots, difficulty, death) in new[] { (-1,1,0), (9,1,0), (0,-1,0), (0,3,0), (0,1,-1), (0,1,2) })
                if (Start(bots, difficulty, death) != NativeExports.InvalidArgument) throw new Exception($"Invalid rules accepted: {bots}/{difficulty}/{death}");
            if (Start(8, 2, 1) != 0) throw new Exception("host command rejected");
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(8));
            while (controller.Status.State != IngameState.Hosting) await Task.Delay(10, timeout.Token);
            await using var client = await SessionClient.ConnectAsync(IPAddress.Loopback, controller.Status.BoundPort, "", timeout.Token);
            var expected = new SessionSettings(3, DeathMode.AnyDeathEndsRun, 8, BotDifficulty.Hard);
            if (client.Settings != expected) throw new Exception($"Welcome expected {expected}; received {client.Settings}");
            if (Start(0, 0, 0) != NativeExports.Unavailable) throw new Exception("active host accepted reconfiguration");
            Console.WriteLine($"Welcome: bots={client.Settings.BotCount}, difficulty={client.Settings.BotDifficulty}, death={client.Settings.DeathMode}");
        }
        finally { await controller.LeaveAsync(); }
    }

    public static async Task GuestCannotEdit()
    {
        var expected = new SessionSettings(3, DeathMode.AllDead, 0, BotDifficulty.Easy);
        await using var owner = new IngameSessionController(expected);
        if (await owner.HostAsync(0, false) != TransitionResult.Started) throw new Exception("host setup");
        var guest = NativeExports.ControllerForTests;
        try
        {
            int port = owner.Status.BoundPort;
            if (await guest.JoinAsync("127.0.0.1", port) != TransitionResult.Started) throw new Exception("guest setup");
            if (Start(8, 2, 1) != NativeExports.Unavailable || guest.Status.State != IngameState.Connected) throw new Exception("connected guest allowed to rehost");
            if (await guest.HostAsync(0, false, default, null, expected with { BotCount = 8 }) != TransitionResult.Busy) throw new Exception("controller bypassed role guard");
            await using var witness = await SessionClient.ConnectAsync(IPAddress.Loopback, port, "");
            if (witness.Settings != expected || guest.Status.State != IngameState.Connected) throw new Exception("guest changed active session");
        }
        finally { await guest.LeaveAsync(); }
    }
}
