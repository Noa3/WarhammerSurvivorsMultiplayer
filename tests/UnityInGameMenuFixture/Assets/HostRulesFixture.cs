using System;
using System.Collections;
using System.Runtime.InteropServices;
using UnityEngine;
using UnityEngine.UI;

public sealed partial class MenuFixture
{
    [DllImport("ingame_ui_fixture", CallingConvention = CallingConvention.Cdecl)] static extern void fixture_rules_editable(int editable);
    [DllImport("ingame_ui_fixture", CallingConvention = CallingConvention.Cdecl)] static extern int fixture_rules_receipt(out int bots, out int difficulty, out int death);

    static bool ClickWidget(GameObject widget) {
        var p = ScreenBounds(widget).center;
        return fixture_click(p.x, p.y) == 1;
    }
    static bool Inside(Rect a, Rect b) => a.width > 0 && a.height > 0 && a.xMin >= b.xMin && a.xMax <= b.xMax && a.yMin >= b.yMin && a.yMax <= b.yMax;

    void CheckRulesLayout(GameObject group, GameObject[] widgets) {
        Rect backdrop = ScreenBounds(group);
        bool ok = Inside(backdrop, new Rect(0, 0, Screen.width, Screen.height));
        // The group is an extension of the panel; its background is not a competing control.
        var canvas = GameObject.Find("WSM Coop Canvas");
        foreach (var graphic in canvas.GetComponentsInChildren<Graphic>()) {
            if (group.transform.IsChildOf(graphic.transform) || graphic.transform.IsChildOf(group.transform)) continue;
            if (backdrop.Overlaps(ScreenBounds(graphic.gameObject))) {
                Debug.Log("RULE_OVERLAP " + graphic.name + " " + ScreenBounds(graphic.gameObject)); ok = false;
            }
        }
        for (int i = 0; i < widgets.Length; ++i) {
            var widget = widgets[i]; var bounds = ScreenBounds(widget);
            var label = widget.GetComponentInChildren<Text>();
            Debug.Log($"RULE_WIDGET {Screen.width}x{Screen.height} {widget.name}: {bounds} '{label.text}'");
            ok &= Inside(bounds, backdrop) && Inside(ScreenBounds(label.gameObject), bounds) && label.enabled && label.color.a > 0 &&
                label.preferredWidth <= label.rectTransform.rect.width + .1f && label.preferredHeight <= label.rectTransform.rect.height + .1f;
            for (int j = 0; j < i; ++j) ok &= !bounds.Overlaps(ScreenBounds(widgets[j]));
        }
        Check("rules_layout_" + Screen.width, ok);
    }
    IEnumerator RunHostRules() {
        Check("rules_hidden_without_capability", !GameObject.Find("WSM Next Host Rules"));
        fixture_rules_editable(1); fixture_tick(); yield return null; Canvas.ForceUpdateCanvases();
        var group = GameObject.Find("WSM Next Host Rules");
        var widgets = Array.ConvertAll(new[]{"WSM Bot Slots", "WSM Bot Difficulty", "WSM Death Rule", "WSM Rules Notice"}, GameObject.Find);
        var host = GameObject.Find("WSM Hosten"); var port = GameObject.Find("WSM Port")?.GetComponent<InputField>();
        bool present = group && host && port && Array.TrueForAll(widgets, w => w && w.GetComponentInChildren<Text>()) &&
            widgets[0].GetComponent<Button>() && widgets[1].GetComponent<Button>() && widgets[2].GetComponent<Button>();
        Check("rules_widgets", present); if (!present) yield break;
        port.text = "8123";
        int ordinal = 0;
        foreach (int width in new[]{1280, 640}) {
            Screen.SetResolution(width, width == 640 ? 360 : 720, FullScreenMode.Windowed);
            for (int i = 0; i < 15; ++i) { fixture_tick(); yield return null; }
            Canvas.ForceUpdateCanvases();
            Check("rules_resolution_" + width, Screen.width == width && Screen.height == (width == 640 ? 360 : 720));
            CheckRulesLayout(group, widgets);
            string screenshot = Environment.GetEnvironmentVariable("WSM_MENU_SCREENSHOT");
            if (!string.IsNullOrEmpty(screenshot)) {
                yield return new WaitForEndOfFrame(); ScreenCapture.CaptureScreenshot(screenshot + "." + width + ".png"); yield return null;
            }
            // Visit the Cartesian product using only clicks, including all three wraps.
            for (int b = 0; b < 9; ++b) {
                for (int d = 0; d < 3; ++d) {
                    int difficulty = (1 + d) % 3;
                    for (int death = 0; death < 2; ++death) {
                        string slots = widgets[0].GetComponentInChildren<Text>().text, level = widgets[1].GetComponentInChildren<Text>().text,
                            ending = widgets[2].GetComponentInChildren<Text>().text;
                        bool labels = slots == "Bot slots: " + b && level == new[]{"Bots: Easy", "Bots: Normal", "Bots: Hard"}[difficulty] &&
                            ending == (death == 0 ? "End when all die" : "End on first death");
                        bool clicked = ClickWidget(host);
                        int count = fixture_rules_receipt(out int rb, out int rd, out int re);
                        Debug.Log($"RULE_RECEIPT {width} #{count} '{slots}' '{level}' '{ending}' -> {fixture_port()} {rb} {rd} {re}");
                        ++ordinal;
                        Check($"rules_host_{width}_{b}_{difficulty}_{death}", clicked && labels && count == ordinal && fixture_port() == 8123 && rb == b && rd == difficulty && re == death && fixture_count(0) == 0);
                        Check("rules_click_death", ClickWidget(widgets[2]));
                    }
                    Check("rules_click_difficulty", ClickWidget(widgets[1]));
                }
                Check("rules_click_slots", ClickWidget(widgets[0]));
            }
            Check("rules_wrap_" + width, widgets[0].GetComponentInChildren<Text>().text == "Bot slots: 0");
        }
        fixture_rules_editable(0); fixture_tick(); yield return null;
        Check("rules_readonly_hidden", !group.activeInHierarchy && !ClickWidget(widgets[0]));
    }
}
