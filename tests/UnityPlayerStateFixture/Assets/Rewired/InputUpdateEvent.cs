using System;
using UnityEngine.Scripting;

namespace Rewired
{
    // Original event double. This tests our subscription lifecycle, not vendor timing.
    public static partial class ReInput
    {
        static Action inputCallbacks;
        public static bool FixtureFailAddBefore, FixtureFailAddAfter;
        public static bool FixtureFailRemoveBefore, FixtureFailRemoveAfter;
        [Preserve] public static event Action InputSourceUpdateEvent
        {
            add
            {
                if (FixtureFailAddBefore) throw new InvalidOperationException("Fixture add before registration");
                inputCallbacks += value;
                if (FixtureFailAddAfter) throw new InvalidOperationException("Fixture add after registration");
            }
            remove
            {
                if (FixtureFailRemoveBefore) throw new InvalidOperationException("Fixture remove before effect");
                inputCallbacks -= value;
                if (FixtureFailRemoveAfter) throw new InvalidOperationException("Fixture remove after effect");
            }
        }
        [Preserve] public static void FixtureFireInput() { inputCallbacks?.Invoke(); }
        public static Action FixtureCaptureInput() => inputCallbacks;
        public static int FixtureInputSubscribers => inputCallbacks?.GetInvocationList().Length ?? 0;
        public static void FixtureResetInputFaults()
        {
            FixtureFailAddBefore = FixtureFailAddAfter = FixtureFailRemoveBefore = FixtureFailRemoveAfter = false;
        }
    }
}
