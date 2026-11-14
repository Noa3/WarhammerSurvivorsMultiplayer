using UnityEngine;
using UnityEngine.EventSystems;

// Independently authored adversarial fixture: selection changes outside the
// current input module, reproducing the observed cross-system focus boundary.
public sealed class ForcedGameSelection : MonoBehaviour
{
    void LateUpdate()
    {
        var target = GameObject.Find("Fixture Underlay");
        if (target && EventSystem.current)
            EventSystem.current.SetSelectedGameObject(target);
    }
}
