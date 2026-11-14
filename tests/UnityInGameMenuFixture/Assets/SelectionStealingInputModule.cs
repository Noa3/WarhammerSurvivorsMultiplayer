using UnityEngine;
using UnityEngine.EventSystems;

// Original adversarial fixture: a game-owned input module keeps its own selected
// control, just as the observed target module did after overlay field selection.
public sealed class SelectionStealingInputModule : StandaloneInputModule
{
    public override void Process()
    {
        base.Process();
        eventSystem.SetSelectedGameObject(GameObject.Find("Fixture Underlay"));
    }
}
