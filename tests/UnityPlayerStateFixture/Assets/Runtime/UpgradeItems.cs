using UnityEngine;
using UnityEngine.Scripting;

// Original test contracts; no game assembly is linked or redistributed.
namespace VampireSurvivors.Data
{
    [Preserve] public enum WeaponType : int { VOID = 0, FixtureData = 7001, FixtureMissingData = 7002 }
}
namespace VampireSurvivors.Data.Weapons
{
    [Preserve] public class WeaponData { }
}
namespace Wilson.UI
{
    [Preserve] public class WSLevelUpItemUI : MonoBehaviour
    {
        [Preserve] private VampireSurvivors.Data.WeaponType _type;
        [Preserve] private VampireSurvivors.Data.Weapons.WeaponData _data;
        [Preserve] private bool _isNew;

        public void Configure(VampireSurvivors.Data.WeaponType type,
                              VampireSurvivors.Data.Weapons.WeaponData data, bool isNew)
        {
            _type = type;
            _data = data;
            _isNew = isNew;
        }
    }
}
