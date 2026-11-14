using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Scripting;
using CharacterController = VampireSurvivors.Objects.Characters.CharacterController;

// Original fixture contracts for metadata-based reference inspection only.
namespace VampireSurvivors.Objects
{
    [Preserve] public class Equipment
    {
        public CharacterController owner;
        public VampireSurvivors.Data.WeaponType kind;
        public bool throwOnOwner;
        public System.Action onReadOwner;
        [Preserve] public CharacterController Owner
        {
            get { if(throwOnOwner)throw new System.InvalidOperationException("Fixture owner failure");onReadOwner?.Invoke();return owner; }
        }
        [Preserve] public VampireSurvivors.Data.WeaponType Type => kind;
    }
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
    [Preserve] public class UIGroup<T> where T : Component
    {
        [Preserve] protected List<T> items;
        public UIGroup(IEnumerable<T> values) { items = new List<T>(values); }
        public void ConfigureItems(List<T> values) { items = values; }
    }
    [Preserve] public class WSLevelUpPage : MonoBehaviour
    {
        [Preserve] private VampireSurvivors.Framework.GameSessionData _gameSession;
        [Preserve] private VampireSurvivors.Framework.LevelUpFactory _levelUpFactory;
        [Preserve] private UIGroup<Transform> _ChoiceContainerGroup;
        [Preserve] private GameObject _FourthOptionHint;
        public void ConfigureHint(GameObject hint) { _FourthOptionHint = hint; }
        public void ConfigureChoiceGroup(UIGroup<Transform> group) { _ChoiceContainerGroup = group; }
        public void Configure(VampireSurvivors.Framework.GameSessionData session,
                              VampireSurvivors.Framework.LevelUpFactory factory)
        {
            _gameSession = session;
            _levelUpFactory = factory;
        }
    }
}
