using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Scripting;
using CharacterController = VampireSurvivors.Objects.Characters.CharacterController;

// Original fixture contracts for metadata-based reference inspection only.
namespace VampireSurvivors.Objects
{
    [Preserve] public class Equipment { }
    [Preserve] public class EquipmentManager
    {
        [field: Preserve] public List<Equipment> ActiveEquipment { get; set; } = new List<Equipment>();
        [field: Preserve] public List<int> WrongEquipment { get; set; } = new List<int>();
    }
    [Preserve] public class CharacterWeaponsManager : EquipmentManager { }
    [Preserve] public class CharacterAccessoriesManager : EquipmentManager { }
}
namespace VampireSurvivors.Framework
{
    [Preserve] public class GameSessionData
    {
        [field: Preserve] public CharacterController ActiveCharacter { get; set; }
    }
    [Preserve] public class LevelUpFactory { }
}
namespace Wilson.UI
{
    [Preserve] public class WSLevelUpPage : MonoBehaviour
    {
        [Preserve] private VampireSurvivors.Framework.GameSessionData _gameSession;
        [Preserve] private VampireSurvivors.Framework.LevelUpFactory _levelUpFactory;
        public void Configure(VampireSurvivors.Framework.GameSessionData session,
                              VampireSurvivors.Framework.LevelUpFactory factory)
        {
            _gameSession = session;
            _levelUpFactory = factory;
        }
    }
}
