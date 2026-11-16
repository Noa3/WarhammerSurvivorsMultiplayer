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
    [Preserve] public class WSLevelUpItemUI : UnityEngine.UI.Selectable
    {
        [Preserve] private VampireSurvivors.Data.WeaponType _type;
        [Preserve] private VampireSurvivors.Data.Weapons.WeaponData _data;
        [Preserve] private bool _isNew;
        [Preserve] private WSLevelUpPage _page;
        [Preserve] private int _currentLevel;
        [Preserve] private bool _isLimitless;
        public void ConfigureLimitless(bool value) { _isLimitless = value; }
        [Preserve] public bool IsWeapon() => _type != VampireSurvivors.Data.WeaponType.VOID && !_isLimitless;
        public int ClickCount;
        public bool ThrowAfterClick;

        public void ConfigureChoice(WSLevelUpPage page, int currentLevel)
        {
            _page = page;
            _currentLevel = currentLevel;
        }

        [Preserve] public void OnClick()
        {
            ++ClickCount;
            _page.RecordChoice(_type);
            if (ThrowAfterClick) throw new System.InvalidOperationException("Fixture callback failed after applying choice");
        }

        public void Configure(VampireSurvivors.Data.WeaponType type,
                              VampireSurvivors.Data.Weapons.WeaponData data, bool isNew)
        {
            _type = type;
            _data = data;
            _isNew = isNew;
        }
    }
}
