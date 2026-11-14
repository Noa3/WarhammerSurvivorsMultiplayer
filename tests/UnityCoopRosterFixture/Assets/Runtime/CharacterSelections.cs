using System.Collections.Generic;
using Rewired;
using UnityEngine;

// Original test contracts, not extracted game implementation.
namespace VampireSurvivors.Data {
    public enum CharacterType { VOID = 0, FixtureAlpha = 37, FixtureBeta = 91 }
    public sealed class PlayerOptionsData {
        public CharacterType SelectedCharacter { get; set; } = CharacterType.FixtureAlpha;
        public Dictionary<Player, VampireSurvivors.Framework.CoopSelections> MultiplayerSelections =
            new Dictionary<Player, VampireSurvivors.Framework.CoopSelections>();
    }
}
namespace VampireSurvivors.Objects {
    public sealed class PlayerOptions {
        public VampireSurvivors.Data.PlayerOptionsData Config { get; } = new VampireSurvivors.Data.PlayerOptionsData();
        public bool IsBought(VampireSurvivors.Data.CharacterType characterType, bool ignoreSkins = false) {
            return characterType == VampireSurvivors.Data.CharacterType.FixtureAlpha ||
                   characterType == VampireSurvivors.Data.CharacterType.FixtureBeta;
        }
    }
}
namespace VampireSurvivors.Framework {
    public sealed class CoopSelections { public VampireSurvivors.Data.CharacterType SelectedCharacter; }
    public sealed class CoopConfig : ScriptableObject { }
}
