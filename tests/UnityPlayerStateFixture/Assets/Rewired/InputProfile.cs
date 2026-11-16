using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Scripting;

// Original metadata contracts; no vendor input implementation is distributed.
namespace Rewired
{
    [Preserve] public static partial class ReInput
    {
        public static bool ready = true;
        public static MappingHelper helper = new MappingHelper();
        [Preserve] public static bool isReady => ready;
        [Preserve] public static MappingHelper mapping => helper;
        [Preserve] public sealed partial class MappingHelper
        {
            public int horizontal = 101, vertical = 103;
            [Preserve] public int GetActionId(string name)
            {
                return name == "Move Horizontal" ? horizontal : name == "Move Vertical" ? vertical : -1;
            }
        }
    }
    [Preserve] public abstract class InputManager_Base : MonoBehaviour
    {
        [Preserve] public Data.UserData userData { get; internal set; }
        public void Configure(Data.UserData data) { userData = data; }
    }
    [Preserve] public sealed class FixtureInputManager : InputManager_Base { }
}
namespace Rewired.Data
{
    [Preserve] public sealed class CustomController_Editor
    {
        public int[] axisIds;
        public bool throwOnRead;
        [Preserve] public int[] GetAxisElementIdentifierIds()
        {
            if(throwOnRead)throw new InvalidOperationException("Fixture axis failure");
            return axisIds;
        }
    }
    [Preserve] public sealed class UserData
    {
        public int[] ids;
        public readonly Dictionary<int, CustomController_Editor> definitions = new Dictionary<int, CustomController_Editor>();
        [Preserve] public int[] GetCustomControllerIds() { return ids; }
        [Preserve] public CustomController_Editor GetCustomControllerById(int id)
        {
            return definitions.TryGetValue(id, out var definition) ? definition : null;
        }
    }
}
