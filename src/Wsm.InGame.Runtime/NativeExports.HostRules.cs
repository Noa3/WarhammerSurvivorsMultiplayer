using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Wsm.Core;

namespace Wsm.InGame.Runtime;

public static partial class NativeExports
{
    [UnmanagedCallersOnly(EntryPoint = "wsm_ingame_host_rules", CallConvs = [typeof(CallConvCdecl)])]
    public static int HostRules(int port, int lanBind, int bots, int difficulty, int death)
    {
        try
        {
            if (bots is < 0 or > 8 || !Enum.IsDefined((BotDifficulty)difficulty) || !Enum.IsDefined((DeathMode)death)) return InvalidArgument;
            return QueueHost(port, lanBind, new SessionSettings(IngameSessionController.MaxClients, (DeathMode)death, bots, (BotDifficulty)difficulty));
        }
        catch { return Unavailable; }
    }
}