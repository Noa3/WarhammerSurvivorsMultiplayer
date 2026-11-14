using System;
using System.Collections.Generic;
using Rewired;

namespace VampireSurvivors.Framework {
    // Original fixture contract. No claim about the game's implementation.
    public sealed class MultiplayerManager {
        public static MultiplayerManager Instance { get; set; }
        public readonly List<Player> roster = new List<Player>();
        public readonly VampireSurvivors.Objects.PlayerOptions _playerOptions = new VampireSurvivors.Objects.PlayerOptions();
        public readonly CoopConfig _coopConfig = UnityEngine.ScriptableObject.CreateInstance<CoopConfig>();
        public int addFault; // 1: before mutation, 2: after mutation
        public bool removeFault;
        public int addCalls, removeCalls;
        public int selectCalls;
        int selectedIndex;
        public bool IsMultiplayer { get { return roster.Count > 1; } }
        public Player GetSelectedPlayer() { return roster.Count == 0 ? null : roster[selectedIndex]; }
        public int GetSelectedPlayerIndex() { return selectedIndex; }
        public void SelectPlayer(Player player, bool exclusiveUIControl = false, bool vibrate = true, float vibrationMS = 200f) {
            if (exclusiveUIControl || vibrate || vibrationMS != 0f) throw new InvalidOperationException("fixture unsafe selection arguments");
            int index = roster.IndexOf(player);
            if (index < 0) throw new InvalidOperationException("fixture unknown selection");
            ++selectCalls; selectedIndex = index;
        }
        public List<Player> GetPlayers() { return roster; }
        public int GetPlayerCount() { return roster.Count; }
        public List<VampireSurvivors.Data.CharacterType> GetCharacterSelections() {
            var values = new List<VampireSurvivors.Data.CharacterType>();
            if (roster.Count <= 1) { values.Add(_playerOptions.Config.SelectedCharacter); return values; }
            foreach (var player in roster) {
                CoopSelections selection;
                values.Add(_playerOptions.Config.MultiplayerSelections.TryGetValue(player, out selection) ?
                    selection.SelectedCharacter : VampireSurvivors.Data.CharacterType.VOID);
            }
            return values;
        }
        public void AddPlayer(Player player) {
            ++addCalls;
            if (addFault == 1) throw new InvalidOperationException("fixture add before");
            roster.Add(player);
            _playerOptions.Config.MultiplayerSelections[player] = new CoopSelections();
            if (addFault == 2) throw new InvalidOperationException("fixture add after");
        }
        private void RemovePlayer(Player player) {
            ++removeCalls;
            if (removeFault) throw new InvalidOperationException("fixture remove");
            roster.Remove(player);
        }
    }
}
