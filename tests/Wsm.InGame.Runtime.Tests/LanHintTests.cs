using System.Net;
using System.Net.NetworkInformation;
using System.Text;
using Wsm.InGame.Runtime;

internal static class LanHintTests
{
    public static Task Filters()
    {
        var invalid = new[] { "0.0.0.0", "0.0.0.1", "127.255.0.2", "169.254.1.9", "224.0.0.1", "255.255.255.255", "::1", "::ffff:192.0.2.1" }
            .Select(s => (IPAddress.Parse(s), true));
        if (LanEndpointHint.Format(invalid, 7780).Length != 0) throw new Exception("unusable candidate");
        var valid = new[] { ("192.0.2.4", false), ("192.0.2.2", true), ("192.0.2.2", true), ("10.0.0.3", false) }
            .Select(p => (IPAddress.Parse(p.Item1), p.Item2));
        if (Encoding.ASCII.GetString(LanEndpointHint.Format(invalid.Concat(valid), 4545)) != "192.0.2.2:4545, 10.0.0.3:4545")
            throw new Exception("unstable, unbounded or duplicate endpoint list");
        return Task.CompletedTask;
    }

    public static async Task Session()
    {
        var host = NativeExports.ControllerForTests;
        await using var guest = new IngameSessionController();
        if (QueueHost() != 0) throw new Exception("LAN host refused");
        try
        {
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
            while (host.Status.State != IngameState.Hosting) await Task.Delay(20, timeout.Token);
            string hint = Read();
            var port = host.Status.BoundPort;
            if (hint != Encoding.ASCII.GetString(LanEndpointHint.Discover(port))) throw new Exception("host ABI omitted locally discovered candidates");
            var local = NetworkInterface.GetAllNetworkInterfaces().Where(n => n.OperationalStatus == OperationalStatus.Up)
                .SelectMany(n => n.GetIPProperties().UnicastAddresses).Select(u => u.Address).ToHashSet();
            string target = "127.0.0.1";
            if (hint.Length != 0)
            {
                foreach (string endpoint in hint.Split(", "))
                {
                    if (!IPEndPoint.TryParse(endpoint, out var ep) || ep.Port != port || !local.Contains(ep.Address) ||
                        IPAddress.IsLoopback(ep.Address) || ep.Address.Equals(IPAddress.Any)) throw new Exception("not a local bound endpoint");
                }
                target = IPEndPoint.Parse(hint.Split(", ")[0]).Address.ToString();
            }
            if (await guest.JoinAsync(target, port) != TransitionResult.Started || guest.LanHint.Length != 0 || Read() != hint)
                throw new Exception("guest admission or host-only hint lifecycle");
            Console.WriteLine(hint.Length == 0 ? "No candidate: verified loopback admission" : "Guest joined the displayed non-loopback endpoint");
            await guest.LeaveAsync();
        }
        finally { await host.LeaveAsync(); }
        if (Read() != "") throw new Exception("stale LAN endpoint after leave");
        if (await host.HostAsync(0, false) != TransitionResult.Started) throw new Exception("loopback host failed");
        try { if (Read() != "") throw new Exception("loopback host advertised a LAN endpoint"); }
        finally { await host.LeaveAsync(); }
    }

    private static unsafe int QueueHost()
    {
        delegate* unmanaged[Cdecl]<int, int, int> start = &NativeExports.Host;
        return start(0, 1);
    }

    private static unsafe string Read()
    {
        delegate* unmanaged[Cdecl]<byte*, int, int> read = &NativeExports.LanHint;
        int size = read(null, 0);
        if (size is < 0 or > 44 || read(null, 1) != NativeExports.InvalidArgument) throw new Exception("invalid ABI length/contract");
        var buffer = new byte[size + 1];
        Array.Fill(buffer, (byte)0xff);
        fixed (byte* p = buffer)
        {
            if (read(p, size) != NativeExports.InsufficientCapacity || buffer.Any(b => b != 0xff) ||
                read(p, buffer.Length) != size || buffer[size] != 0) throw new Exception("invalid ABI copy");
        }
        return Encoding.ASCII.GetString(buffer, 0, size);
    }
}
