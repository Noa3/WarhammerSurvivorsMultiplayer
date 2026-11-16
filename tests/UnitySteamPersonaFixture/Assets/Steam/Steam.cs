using System;
namespace Steamworks {
    public struct HSteamUser {
        public int value;
        public static explicit operator int(HSteamUser user) { if (SteamAPI.fault == 3) throw new Exception("fixture"); return user.value; }
    }
    public static class SteamAPI {
        public static bool running;
        public static int user;
        public static int fault;
        public static bool IsSteamRunning() { if(fault == 1) throw new Exception("fixture"); return running; }
        public static HSteamUser GetHSteamUser() { if(fault == 2) throw new Exception("fixture"); return new HSteamUser { value = user }; }
    }
    public static class SteamFriends {
        public static string name;
        public static int calls;
        public static string GetPersonaName() { ++calls; if(SteamAPI.fault == 4) throw new Exception("fixture"); return name; }
    }
}
