using UnityEngine.Scripting;

namespace I2.Loc
{
    // Original SDK fixture, not the game's localization implementation. Its public
    // metadata contract is verified separately against the installed game.
    public static class LocalizationManager
    {
        [Preserve]
        public static string CurrentLanguageCode { get; set; } = "en";
    }
}
