using System.Collections.Generic;

namespace Rewired {
    // Original fixture contract, not the vendor's implementation.
    public sealed class Player {
        public Player(int value) { id = value; }
        public int id { get; }
    }
    public static class ReInput {
        public sealed class PlayerHelper {
            public IList<Player> configured = new List<Player>();
            public IList<Player> GetPlayers(bool includeSystemPlayer) { return configured; }
        }
        public static PlayerHelper players { get; set; }
    }
}
