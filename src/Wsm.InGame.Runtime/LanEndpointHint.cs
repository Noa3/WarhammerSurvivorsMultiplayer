using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text;

namespace Wsm.InGame.Runtime;

internal static class LanEndpointHint
{
    // Passive OS inventory only. A local unicast address is a candidate, not a reachability test.
    internal static byte[] Discover(int port)
    {
        var candidates = new List<(IPAddress Address, bool HasGateway)>();
        try
        {
            foreach (var adapter in NetworkInterface.GetAllNetworkInterfaces())
            {
                try
                {
                    if (adapter.OperationalStatus != OperationalStatus.Up ||
                        adapter.NetworkInterfaceType is NetworkInterfaceType.Loopback or NetworkInterfaceType.Tunnel) continue;
                    var properties = adapter.GetIPProperties();
                    bool gateway = properties.GatewayAddresses.Any(g => g.Address.AddressFamily == AddressFamily.InterNetwork && !g.Address.Equals(IPAddress.Any));
                    foreach (var unicast in properties.UnicastAddresses)
                    {
                        if (OperatingSystem.IsWindows() && unicast.DuplicateAddressDetectionState != DuplicateAddressDetectionState.Preferred) continue;
                        candidates.Add((unicast.Address, gateway));
                    }
                }
                catch (Exception e) when (e is NetworkInformationException or NotSupportedException) { /* adapter disappeared or lacks metadata */ }
            }
        }
        catch (Exception e) when (e is NetworkInformationException or NotSupportedException) { /* preserve any candidates collected */ }
        return Format(candidates, port);
    }

    internal static byte[] Format(IEnumerable<(IPAddress Address, bool HasGateway)> candidates, int port)
    {
        // Two endpoints fit on a separate line of the existing status label. Prefer an adapter
        // with an IPv4 gateway over isolated virtual switches, without requiring a default route.
        var addresses = candidates.Where(c => c.Address.AddressFamily == AddressFamily.InterNetwork)
            .Where(c => c.Address.GetAddressBytes() is [> 0 and < 224 and not 127, _, _, _] and not [169, 254, _, _])
            .OrderByDescending(c => c.HasGateway).ThenBy(c => c.Address.ToString(), StringComparer.Ordinal)
            .Select(c => c.Address).Distinct().Take(2);
        return Encoding.ASCII.GetBytes(string.Join(", ", addresses.Select(a => FormattableString.Invariant($"{a}:{port}"))));
    }
}
