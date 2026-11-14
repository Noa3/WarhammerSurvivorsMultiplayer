using UnityEngine.EventSystems;

// Original fixture: selected controls receive keyboard updates, but this module
// deliberately does not perform pointer selection for injected overlay widgets.
public sealed class KeyboardOnlyInputModule : StandaloneInputModule
{
    public override void Process() => SendUpdateEventToSelectedObject();
}
