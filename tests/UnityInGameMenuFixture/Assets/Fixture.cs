using System;
using System.Collections;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;

public sealed class MenuFixture : MonoBehaviour
{
    [DllImport("ingame_ui_fixture", CallingConvention = CallingConvention.Cdecl)] static extern int fixture_init();
    [DllImport("ingame_ui_fixture", CallingConvention = CallingConvention.Cdecl)] static extern void fixture_tick();
    [DllImport("ingame_ui_fixture", CallingConvention = CallingConvention.Cdecl)] static extern void fixture_input(int foreground, int mouseDown, int f6Down, float x, float y);
    [DllImport("ingame_ui_fixture", CallingConvention = CallingConvention.Cdecl)] static extern int fixture_click(float x, float y);
    [DllImport("ingame_ui_fixture", CallingConvention = CallingConvention.Cdecl)] static extern int fixture_count(int action);
    [DllImport("ingame_ui_fixture", CallingConvention = CallingConvention.Cdecl)] static extern int fixture_port();
    [DllImport("ingame_ui_fixture", CallingConvention = CallingConvention.Cdecl)] static extern int fixture_address_is([MarshalAs(UnmanagedType.LPStr)] string expected);
    [DllImport("ingame_ui_fixture", CallingConvention = CallingConvention.Cdecl)] static extern void fixture_shutdown();
    [DllImport("ingame_ui_fixture", CallingConvention = CallingConvention.Cdecl)] static extern void fixture_fail_language_label();
    [DllImport("ingame_ui_fixture", CallingConvention = CallingConvention.Cdecl)] static extern void fixture_camera_callbacks(int mode, int state);
    [DllImport("ingame_ui_fixture", CallingConvention = CallingConvention.Cdecl)] static extern void fixture_camera_effective(int state);
    [DllImport("ingame_ui_fixture", CallingConvention = CallingConvention.Cdecl)] static extern void fixture_name_state(int state);
    [DllImport("ingame_ui_fixture", CallingConvention = CallingConvention.Cdecl)] static extern void fixture_name_callbacks(int local, int roster);
    [DllImport("ingame_ui_fixture", CallingConvention = CallingConvention.Cdecl)] static extern int fixture_init_without_name_metadata();
    int failures;
    void Check(string name, bool ok) { Debug.Log("MENU_FIXTURE " + name + "=" + (ok ? "PASS" : "FAIL")); if (!ok) ++failures; }
    static Rect ScreenBounds(GameObject go) {
        var corners = new Vector3[4]; go.GetComponent<RectTransform>().GetWorldCorners(corners);
        var min = RectTransformUtility.WorldToScreenPoint(null, corners[0]);
        var max = RectTransformUtility.WorldToScreenPoint(null, corners[2]);
        return Rect.MinMaxRect(min.x, min.y, max.x, max.y);
    }
    static bool CameraLayoutFits(GameObject panel, GameObject own, GameObject shared, GameObject state, GameObject networkState) {
        var bounds = ScreenBounds(panel); var a = ScreenBounds(own); var b = ScreenBounds(shared);
        var c = ScreenBounds(state); var d = ScreenBounds(networkState);
        return bounds.xMin >= 0 && bounds.yMin >= 0 && bounds.xMax <= Screen.width && bounds.yMax <= Screen.height &&
            bounds.Contains(a.min) && bounds.Contains(a.max) && bounds.Contains(b.min) && bounds.Contains(b.max) &&
            bounds.Contains(c.min) && bounds.Contains(c.max) && !a.Overlaps(b) && !a.Overlaps(c) && !b.Overlaps(c) &&
            !a.Overlaps(d) && !b.Overlaps(d) && !c.Overlaps(d);
    }
    static bool NamesLayoutFits(GameObject panel, Text localName, GameObject namesHud) {
        if (!panel || !localName || !namesHud) return false;
        var bounds = ScreenBounds(panel); var local = ScreenBounds(localName.gameObject); var hud = ScreenBounds(namesHud);
        var title = ScreenBounds(GameObject.Find("WSM Title"));
        if (!bounds.Contains(local.min) || !bounds.Contains(local.max) || local.Overlaps(title) || hud.Overlaps(bounds) ||
            hud.xMin < 0 || hud.yMin < 0 || hud.xMax > Screen.width || hud.yMax > Screen.height) return false;
        foreach (var label in namesHud.GetComponentsInChildren<Text>()) {
            var labelBounds = ScreenBounds(label.gameObject);
            if (!hud.Contains(labelBounds.min) || !hud.Contains(labelBounds.max) || label.raycastTarget ||
                label.preferredWidth > label.rectTransform.rect.width + .1f) return false;
        }
        return true;
    }
    IEnumerator Start()
    {
        // Native UI must load or discover a Unity-owned font; the fixture does not preload one.
        Debug.Log("MENU_FIXTURE fonts_before=" + Resources.FindObjectsOfTypeAll<Font>().Length);
        if (Environment.GetEnvironmentVariable("WSM_MENU_NO_EVENT_SYSTEM") != "1")
            new GameObject("Fixture EventSystem", typeof(EventSystem), Environment.GetEnvironmentVariable("WSM_MENU_STEAL_SELECTION") == "1" ? typeof(SelectionStealingInputModule) : Environment.GetEnvironmentVariable("WSM_MENU_KEYBOARD_ONLY") == "1" ? typeof(KeyboardOnlyInputModule) : typeof(StandaloneInputModule));
        if (Environment.GetEnvironmentVariable("WSM_MENU_FORCE_SELECTION") == "1")
            new GameObject("Fixture Foreign Focus Controller", typeof(ForcedGameSelection));
        var underCanvas = new GameObject("Fixture Underlay Canvas", typeof(RectTransform), typeof(Canvas), typeof(GraphicRaycaster));
        underCanvas.GetComponent<Canvas>().renderMode = RenderMode.ScreenSpaceOverlay;
        var underlay = new GameObject("Fixture Underlay", typeof(RectTransform), typeof(Image), typeof(Button));
        underlay.transform.SetParent(underCanvas.transform, false);
        underlay.GetComponent<Image>().color = new Color(0.07f, 0.09f, 0.13f, 1f);
        var underRect = underlay.GetComponent<RectTransform>();
        underRect.anchorMin = Vector2.zero; underRect.anchorMax = Vector2.one; underRect.offsetMin = Vector2.zero; underRect.offsetMax = Vector2.zero;
        int underClicks = 0; underlay.GetComponent<Button>().onClick.AddListener(() => ++underClicks);
        int wrongThread = 0;
        var worker = new System.Threading.Thread(() => wrongThread = fixture_init());
        worker.Start(); worker.Join();
        Check("wrong_thread_refused", wrongThread != 0);
        int initialized = -1;
        try { initialized = fixture_init(); } catch (Exception e) { Debug.LogError("MENU_FIXTURE init exception " + e); }
        Check("native_init", initialized == 0);
        if (initialized != 0) { Finish(); yield break; }
        for (int i = 0; i < 5; ++i) { fixture_tick(); yield return null; }
        if (Array.IndexOf(Environment.GetCommandLineArgs(), "--os-input-test") >= 0) {
            if (Environment.GetEnvironmentVariable("WSM_MENU_MODAL") == "1") {
                var modal = new GameObject("Fixture Higher Modal", typeof(RectTransform), typeof(Canvas), typeof(GraphicRaycaster), typeof(Image));
                var modalCanvas = modal.GetComponent<Canvas>(); modalCanvas.renderMode = RenderMode.ScreenSpaceOverlay; modalCanvas.sortingOrder = 31000;
                var modalRect = modal.GetComponent<RectTransform>(); modalRect.anchorMin = Vector2.zero; modalRect.anchorMax = Vector2.one;
                modalRect.offsetMin = Vector2.zero; modalRect.offsetMax = Vector2.zero;
                yield return null;
            }
            yield return RunOsInputTest();
            fixture_shutdown(); yield return null;
            Check("os_shutdown_destroyed", !GameObject.Find("WSM Coop Canvas"));
            Finish(); yield break;
        }
        var canvas = GameObject.Find("WSM Coop Canvas");
        var host = GameObject.Find("WSM Hosten");
        var join = GameObject.Find("WSM Beitreten");
        var leave = GameObject.Find("WSM Verlassen");
        var cameraOwn = GameObject.Find("WSM Camera Own");
        var cameraShared = GameObject.Find("WSM Camera Shared");
        var cameraStatus = GameObject.Find("WSM Camera Status")?.GetComponent<Text>();
        var address = GameObject.Find("WSM Address")?.GetComponent<InputField>();
        var port = GameObject.Find("WSM Port")?.GetComponent<InputField>();
        Check("open_owns_event_system", EventSystem.current && EventSystem.current.gameObject.name == "WSM Owned EventSystem");
        Check("render_tree", canvas && host && join && leave && address && port && cameraOwn && cameraShared && cameraStatus);
        var localName = GameObject.Find("WSM Local Player Name")?.GetComponent<Text>();
        var namesHud = GameObject.Find("WSM Player Names HUD");
        var firstName = GameObject.Find("WSM Player Name 0")?.GetComponent<Text>();
        var secondName = GameObject.Find("WSM Player Name 1")?.GetComponent<Text>();
        Check("player_names_render_actual_callbacks", localName && namesHud && firstName && secondName &&
            localName.text == "You: Fixture Captain" && firstName.text == "Fixture Captain" && secondName.text == "Rune <b>Hunter</b>");
        Check("player_names_are_literal_not_rich_text", firstName && secondName && !firstName.supportRichText && !secondName.supportRichText);
        if (localName && namesHud && firstName && secondName) {
            Check("player_names_do_not_capture_input", !namesHud.GetComponent<Image>().raycastTarget && !firstName.raycastTarget && !secondName.raycastTarget && !localName.raycastTarget);
            fixture_name_state(1); fixture_tick();
            Check("player_names_empty_uses_local_fallback", localName.text == "You: Player" && firstName.text == "Player" && !secondName.gameObject.activeSelf);
            fixture_name_callbacks(0, 0); fixture_tick();
            Check("player_names_callback_removal_from_empty_hides", !namesHud.activeSelf && !localName.gameObject.activeSelf);
            fixture_name_callbacks(1, 1); fixture_name_state(0); fixture_tick();
            Check("player_names_callbacks_restore_same_widgets", namesHud.activeSelf && localName.gameObject.activeSelf &&
                firstName.text == "Fixture Captain" && secondName.gameObject.activeSelf && GameObject.Find("WSM Player Name 0").GetComponent<Text>() == firstName);
            fixture_name_state(4); fixture_tick();
            Check("player_names_departure_clears_remote_row", firstName.text == "Fixture Captain" && !secondName.gameObject.activeSelf);
            fixture_name_state(2); fixture_tick();
            Check("player_names_long_names_fit_without_utf16_split", firstName.text.EndsWith("…", StringComparison.Ordinal) && firstName.preferredWidth <= 296.1f && localName.preferredWidth <= 540.1f);
            fixture_name_state(3); fixture_tick();
            firstName.font.RequestCharactersInTexture("勇者", firstName.fontSize);
            Check("player_names_unicode_text_preserved", firstName.text == "勇者" && localName.text == "You: 勇者");
            Check("player_names_cjk_glyphs_in_english_ui", firstName.font.HasCharacter('勇') && firstName.font.HasCharacter('者'));
            fixture_name_state(0); fixture_tick();
        }
        fixture_input(0, 0, 0, 0, 0); fixture_input(1, 0, 0, 0, 0); fixture_input(1, 0, 1, 0, 0);
        Check("player_names_visible_with_menu_closed", namesHud && namesHud.activeInHierarchy && localName && !localName.gameObject.activeInHierarchy);
        fixture_input(1, 0, 0, 0, 0); fixture_input(1, 0, 1, 0, 0); fixture_input(0, 0, 0, 0, 0);
        Check("player_names_menu_reopens_same_label", localName && localName.gameObject.activeInHierarchy && GameObject.Find("WSM Local Player Name").GetComponent<Text>() == localName);
        if (!canvas || !host || !join || !leave || !address || !port || !cameraOwn || !cameraShared || !cameraStatus) { fixture_shutdown(); Finish(); yield break; }
        var title = GameObject.Find("WSM Title").GetComponent<Text>();
        Check("game_language_english_initial", title.text == "Co-op connection" && host.GetComponentInChildren<Text>().text == "Host");
        address.text = "192.0.2.7"; port.text = "8123";
        I2.Loc.LocalizationManager.CurrentLanguageCode = "de";
        fixture_tick(); yield return null;
        Check("game_language_german_status", GameObject.Find("WSM Status").GetComponent<Text>().text == "Fixture: keine Netzwerkverbindung");
        Check("game_language_german_live", title.text == "Koop-Verbindung" && host.GetComponentInChildren<Text>().text == "Hosten");
        Check("player_names_localized_labels_not_personas", localName && localName.text == "Du: Fixture Captain" &&
            firstName && firstName.text == "Fixture Captain" && GameObject.Find("WSM Player Names Title").GetComponent<Text>().text == "Spieler");
        Check("language_switch_preserves_input_and_actions", address.text == "192.0.2.7" && port.text == "8123" && fixture_count(0) == 0 && fixture_count(1) == 0 && fixture_count(2) == 0);
        I2.Loc.LocalizationManager.CurrentLanguageCode = "en-US";
        fixture_tick(); yield return null;
        Check("game_language_english_live", title.text == "Co-op connection" && join.GetComponentInChildren<Text>().text == "Join");
        address.text = "127.0.0.1"; port.text = "7780";
        fixture_fail_language_label();
        I2.Loc.LocalizationManager.CurrentLanguageCode = "fr";
        fixture_tick(); yield return null;
        fixture_tick(); yield return null;
        Check("language_recovers_after_one_shot_setter_failure", title.text == "Connexion coop" && host.GetComponentInChildren<Text>().text == "Héberger" &&
            cameraOwn.GetComponentInChildren<Text>().text == "Caméra personnelle" && cameraStatus.text == "Caméra inactive");
        I2.Loc.LocalizationManager.CurrentLanguageCode = "en";
        fixture_tick(); yield return null;
        var languageTitles = new[] {
            new[] { "en", "Co-op connection" }, new[] { "de", "Koop-Verbindung" },
            new[] { "fr", "Connexion coop" }, new[] { "it", "Connessione co-op" },
            new[] { "es", "Conexión cooperativa" }, new[] { "ja", "協力接続" },
            new[] { "ko", "협동 연결" }, new[] { "pl", "Połączenie kooperacyjne" },
            new[] { "pt-BR", "Conexão cooperativa" }, new[] { "ru", "Кооперативное соединение" },
            new[] { "zh-CN", "合作连接" }, new[] { "tr", "Eşli bağlantı" }, new[] { "uk", "Спільне з'єднання" }
        };
        var cameraLabels = new[] {
            new[] { "Own camera", "Shared camera", "Camera inactive" }, new[] { "Eigene Kamera", "Gemeinsame Kamera", "Kamera inaktiv" },
            new[] { "Caméra personnelle", "Caméra partagée", "Caméra inactive" }, new[] { "Telecamera personale", "Telecamera condivisa", "Telecamera inattiva" },
            new[] { "Cámara propia", "Cámara compartida", "Cámara inactiva" }, new[] { "自分のカメラ", "共有カメラ", "カメラ無効" },
            new[] { "내 카메라", "공유 카메라", "카메라 비활성" }, new[] { "Własna kamera", "Wspólna kamera", "Kamera nieaktywna" },
            new[] { "Câmera própria", "Câmera compartilhada", "Câmera inativa" }, new[] { "Своя камера", "Общая камера", "Камера неактивна" },
            new[] { "自己的镜头", "共享镜头", "镜头未启用" }, new[] { "Kendi kameram", "Ortak kamera", "Kamera etkin değil" },
            new[] { "Власна камера", "Спільна камера", "Камера неактивна" }
        };
        int cameraLanguage = 0;
        foreach (var item in languageTitles) {
            I2.Loc.LocalizationManager.CurrentLanguageCode = item[0];
            fixture_tick(); yield return null;
            Check("language_title_" + item[0], title.text == item[1]);
            var expectedCamera = cameraLabels[cameraLanguage++];
            Check("camera_language_" + item[0], cameraOwn.GetComponentInChildren<Text>().text == expectedCamera[0] &&
                cameraShared.GetComponentInChildren<Text>().text == expectedCamera[1] && cameraStatus.text == expectedCamera[2] &&
                GameObject.Find("WSM Camera Own") == cameraOwn && GameObject.Find("WSM Camera Shared") == cameraShared &&
                address.text == "127.0.0.1" && port.text == "7780");
        }
        I2.Loc.LocalizationManager.CurrentLanguageCode = "unknown-game-language";
        fixture_tick(); yield return null;
        Check("unknown_language_falls_back_to_english", title.text == "Co-op connection");
        I2.Loc.LocalizationManager.CurrentLanguageCode = "zh-CN";
        fixture_tick(); yield return new WaitForEndOfFrame();
        title.font.RequestCharactersInTexture("中文", title.fontSize);
        Check("chinese_font_has_glyphs", title.font.HasCharacter('中') && title.font.HasCharacter('文'));
        I2.Loc.LocalizationManager.CurrentLanguageCode = "en";
        fixture_tick(); yield return null;
        Check("defaults", address.text == "127.0.0.1" && port.text == "7780");
        Screen.SetResolution(1920, 1080, FullScreenMode.Windowed);
        for (int i = 0; i < 15; ++i) { fixture_tick(); yield return null; }
        var expectedScale = Mathf.Min(Screen.width / 1280f, Screen.height / 720f);
        Check("canvas_scales_after_resize", Mathf.Abs(canvas.GetComponent<Canvas>().scaleFactor - expectedScale) < .01f && expectedScale > 1.1f);
        Screen.SetResolution(1280, 720, FullScreenMode.Windowed);
        for (int i = 0; i < 15; ++i) { fixture_tick(); yield return null; }
        Check("canvas_scale_restored", Mathf.Abs(canvas.GetComponent<Canvas>().scaleFactor - 1f) < .01f);
        var status = GameObject.Find("WSM Status")?.GetComponent<Text>();
        Check("camera_layout_default", CameraLayoutFits(GameObject.Find("WSM Coop Panel"), cameraOwn, cameraShared, cameraStatus.gameObject, status.gameObject));
        Check("player_names_layout_default", NamesLayoutFits(GameObject.Find("WSM Coop Panel"), localName, namesHud));
        Screen.SetResolution(640, 360, FullScreenMode.Windowed);
        for (int i = 0; i < 15; ++i) { fixture_tick(); yield return null; }
        Check("camera_layout_minimum", Screen.width == 640 && Screen.height == 360 &&
            CameraLayoutFits(GameObject.Find("WSM Coop Panel"), cameraOwn, cameraShared, cameraStatus.gameObject, status.gameObject));
        fixture_name_state(2); fixture_tick(); yield return null;
        Check("player_names_four_long_names_layout_minimum", namesHud && namesHud.GetComponentsInChildren<Text>().Length == 5 &&
            NamesLayoutFits(GameObject.Find("WSM Coop Panel"), localName, namesHud));
        fixture_name_state(5); fixture_tick(); yield return null;
        var nineNames = namesHud ? namesHud.GetComponentsInChildren<Text>() : new Text[0];
        bool nineNamesMatch = nineNames.Length == 10;
        for (int i = 0; i < 9; ++i) {
            var row = GameObject.Find("WSM Player Name " + i)?.GetComponent<Text>();
            nineNamesMatch &= row && row.text == (i == 0 ? "Host" : "Guest " + i) && !row.supportRichText && !row.raycastTarget;
        }
        Check("player_names_all_nine_core_members_literal", nineNamesMatch);
        Check("player_names_nine_members_layout_minimum", nineNamesMatch && NamesLayoutFits(GameObject.Find("WSM Coop Panel"), localName, namesHud));
        fixture_name_state(0); fixture_tick();
        Screen.SetResolution(1280, 720, FullScreenMode.Windowed);
        for (int i = 0; i < 15; ++i) { fixture_tick(); yield return null; }
        Check("live_status", status && status.text == "Fixture: no network connection");
        var ownPoint = RectTransformUtility.WorldToScreenPoint(null, cameraOwn.transform.position);
        var sharedPoint = RectTransformUtility.WorldToScreenPoint(null, cameraShared.transform.position);
        Check("camera_own_click", fixture_click(ownPoint.x, ownPoint.y) == 1 && fixture_count(3) == 1 && fixture_count(4) == 0);
        fixture_input(0, 0, 0, sharedPoint.x, sharedPoint.y);
        fixture_input(1, 0, 0, sharedPoint.x, sharedPoint.y);
        fixture_input(1, 1, 0, sharedPoint.x, sharedPoint.y);
        fixture_input(1, 1, 0, sharedPoint.x, sharedPoint.y);
        Check("camera_shared_input_edge_once", fixture_count(4) == 1 && fixture_count(3) == 1);
        fixture_tick();
        Check("camera_request_does_not_fake_status", cameraStatus.text == "Camera inactive");
        var effectiveStates = new[] { "Camera inactive", "Camera active", "Spectating", "Camera unavailable" };
        for (int i = 0; i < effectiveStates.Length; ++i) {
            fixture_camera_effective(i); fixture_tick();
            Check("camera_effective_" + i, cameraStatus.text == effectiveStates[i]);
        }
        fixture_camera_callbacks(0, 0); fixture_tick();
        Check("camera_callbacks_absent", !cameraOwn.activeSelf && !cameraShared.activeSelf && cameraStatus.text == "Camera unavailable" &&
            fixture_click(ownPoint.x, ownPoint.y) == 0 && fixture_count(3) == 1 && fixture_count(4) == 1);
        fixture_camera_callbacks(1, 1); fixture_camera_effective(0); fixture_tick();
        Check("camera_callbacks_restored_identity", cameraOwn.activeSelf && cameraShared.activeSelf && GameObject.Find("WSM Camera Own") == cameraOwn && cameraStatus.text == "Camera inactive");
        address.Select(); address.ActivateInputField();
        yield return null;
        address.text = "127.0.0.1";
        Check("endpoint_views_are_read_only", address.readOnly && port.readOnly);
        foreach (char c in "192.0.2.7") address.ProcessEvent(new Event { type = EventType.KeyDown, character = c });
        Check("foreign_unity_input_cannot_edit_endpoint", address.text == "127.0.0.1");
        address.text = "192.0.2.7";
        port.text = "8123";
        var rc = canvas.GetComponent<GraphicRaycaster>();
        var eventData = new PointerEventData(EventSystem.current);
        var hits = new List<RaycastResult>();
        eventData.position = RectTransformUtility.WorldToScreenPoint(null, host.transform.position);
        rc.Raycast(eventData, hits);
        Check("real_raycaster_hit", hits.Count != 0 && hits[0].gameObject == host);
        hits.Clear(); EventSystem.current.RaycastAll(eventData, hits);
        Check("blocks_underlying_ui", hits.Count != 0 && hits[0].gameObject == host);
        if (hits.Count != 0) ExecuteEvents.Execute(hits[0].gameObject, eventData, ExecuteEvents.pointerClickHandler);
        Check("unity_click_does_not_hit_underlay", underClicks == 0 && fixture_count(0) == 0);
        var panelPoint = RectTransformUtility.WorldToScreenPoint(null, GameObject.Find("WSM Coop Panel").transform.position) + new Vector2(260, -160);
        eventData.position = panelPoint; hits.Clear(); EventSystem.current.RaycastAll(eventData, hits);
        Check("panel_blocks_underlying_ui", hits.Count != 0 && hits[0].gameObject.name == "WSM Coop Panel");
        Check("outside_no_callback", fixture_click(-1000, -1000) == 0 && fixture_count(0) == 0 && fixture_count(1) == 0 && fixture_count(2) == 0);
        foreach (var b in new[] { host, join, leave }) {
            var p = RectTransformUtility.WorldToScreenPoint(null, b.transform.position);
            Check("hit_" + b.name, fixture_click(p.x, p.y) == 1);
        }
        Check("callbacks_once", fixture_count(0) == 1 && fixture_count(1) == 1 && fixture_count(2) == 1);
        Check("callback_arguments", fixture_port() == 8123 && fixture_address_is("192.0.2.7") == 1);
        port.text = "invalid";
        var hp = RectTransformUtility.WorldToScreenPoint(null, host.transform.position);
        fixture_click(hp.x, hp.y);
        Check("invalid_port_no_callback", fixture_count(0) == 1);
        port.text = "8123";
        fixture_input(0, 1, 0, hp.x, hp.y);
        Check("background_press_ignored", fixture_count(0) == 1);
        fixture_input(1, 0, 0, hp.x, hp.y); fixture_input(1, 1, 0, hp.x, hp.y);
        fixture_input(1, 1, 0, hp.x, hp.y);
        Check("foreground_edge_once", fixture_count(0) == 2);
        fixture_input(0, 1, 0, hp.x, hp.y); fixture_input(1, 1, 0, hp.x, hp.y);
        Check("focus_with_held_mouse_ignored", fixture_count(0) == 2);
        fixture_input(1, 0, 0, hp.x, hp.y); fixture_input(1, 1, 0, hp.x, hp.y);
        Check("next_mouse_edge", fixture_count(0) == 3);
        fixture_input(1, 0, 1, hp.x, hp.y);
        Check("hidden_restores_game_event_system", EventSystem.current && EventSystem.current.gameObject.name == "Fixture EventSystem");
        Check("f6_hides_panel", !GameObject.Find("WSM Coop Panel") && fixture_click(hp.x, hp.y) == 0);
        fixture_input(1, 0, 0, hp.x, hp.y); fixture_input(1, 0, 1, hp.x, hp.y);
        Check("reopened_owns_event_system", EventSystem.current && EventSystem.current.gameObject.name == "WSM Owned EventSystem");
        Check("f6_restores_panel", GameObject.Find("WSM Coop Panel"));
        fixture_input(0, 0, 0, hp.x, hp.y);
        fixture_tick();
        yield return new WaitForEndOfFrame();
        string screenshot = Environment.GetEnvironmentVariable("WSM_MENU_SCREENSHOT");
        if (!string.IsNullOrEmpty(screenshot)) ScreenCapture.CaptureScreenshot(screenshot);
        for (int i = 0; i < 20; ++i) yield return null;
        Check("screenshot_written", !string.IsNullOrEmpty(screenshot) && System.IO.File.Exists(screenshot));
        fixture_shutdown();
        yield return null;
        Check("shutdown_restores_game_event_system", EventSystem.current && EventSystem.current.gameObject.name == "Fixture EventSystem");
        Check("shutdown_destroyed_root", !GameObject.Find("WSM Coop Canvas"));
        int missingNamesInit = fixture_init_without_name_metadata();
        if (missingNamesInit == 0) fixture_tick();
        // Reinitialization creates new graphics; let Unity register their raycasts.
        yield return null;
        Canvas.ForceUpdateCanvases();
        Check("player_names_missing_literal_metadata_keeps_menu", missingNamesInit == 0 && GameObject.Find("WSM Hosten") && !GameObject.Find("WSM Player Names HUD") && !GameObject.Find("WSM Local Player Name"));
        int hostBeforeMissingNames = fixture_count(0);
        var fallbackHost = GameObject.Find("WSM Hosten");
        if (missingNamesInit == 0 && fallbackHost) {
            var fallbackPoint = RectTransformUtility.WorldToScreenPoint(null, fallbackHost.transform.position);
            fixture_click(fallbackPoint.x, fallbackPoint.y);
        }
        Check("player_names_missing_metadata_host_action_works", missingNamesInit == 0 && fixture_count(0) == hostBeforeMissingNames + 1);
        fixture_name_callbacks(0, 0); fixture_tick();
        Check("player_names_legacy_without_literal_metadata_keeps_menu", missingNamesInit == 0 && GameObject.Find("WSM Hosten") && !GameObject.Find("WSM Player Names HUD"));
        fixture_shutdown(); yield return null;
        Check("player_names_missing_metadata_shutdown_clean", !GameObject.Find("WSM Coop Canvas") && EventSystem.current && EventSystem.current.gameObject.name == "Fixture EventSystem");
        if (Array.IndexOf(Environment.GetCommandLineArgs(), "--force-failure") >= 0) Check("forced_failure", false);
        Finish();
    }
    [Serializable] sealed class OsState {
        public int width, height, hostCount, joinCount, leaveCount;
        public bool panelVisible, modalTopmost;
        public float hostX, hostY, joinX, joinY, leaveX, leaveY, addressX, addressY, portX, portY;
    }
    IEnumerator RunOsInputTest() {
        string statePath = Environment.GetEnvironmentVariable("WSM_MENU_STATE");
        var host = GameObject.Find("WSM Hosten"); var join = GameObject.Find("WSM Beitreten"); var leave = GameObject.Find("WSM Verlassen");
        var address = GameObject.Find("WSM Address"); var port = GameObject.Find("WSM Port");
        if (string.IsNullOrEmpty(statePath) || !host || !join || !leave || !address || !port) { Check("os_state_setup", false); yield break; }
        var hp = RectTransformUtility.WorldToScreenPoint(null, host.transform.position);
        var jp = RectTransformUtility.WorldToScreenPoint(null, join.transform.position);
        var lp = RectTransformUtility.WorldToScreenPoint(null, leave.transform.position);
        var state = new OsState { width = Screen.width, height = Screen.height, hostX = hp.x, hostY = hp.y, joinX = jp.x, joinY = jp.y, leaveX = lp.x, leaveY = lp.y };
        if (Environment.GetEnvironmentVariable("WSM_MENU_MODAL") == "1" && EventSystem.current) {
            var pointer = new PointerEventData(EventSystem.current) { position = hp };
            var hits = new List<RaycastResult>(); EventSystem.current.RaycastAll(pointer, hits);
            state.modalTopmost = hits.Count > 0 && hits[0].gameObject.name == "Fixture Higher Modal";
        }
        float start = Time.realtimeSinceStartup, nextWrite = 0;
        while (Time.realtimeSinceStartup - start < 45) {
            fixture_tick();
            state.panelVisible = GameObject.Find("WSM Coop Panel") != null;
            var ap = RectTransformUtility.WorldToScreenPoint(null, address.transform.position);
            var pp = RectTransformUtility.WorldToScreenPoint(null, port.transform.position);
            state.width = Screen.width; state.height = Screen.height;
            state.addressX = ap.x; state.addressY = ap.y; state.portX = pp.x; state.portY = pp.y;
            hp = RectTransformUtility.WorldToScreenPoint(null, host.transform.position);
            jp = RectTransformUtility.WorldToScreenPoint(null, join.transform.position);
            lp = RectTransformUtility.WorldToScreenPoint(null, leave.transform.position);
            state.hostX = hp.x; state.hostY = hp.y; state.joinX = jp.x; state.joinY = jp.y; state.leaveX = lp.x; state.leaveY = lp.y;
            state.hostCount = fixture_count(0); state.joinCount = fixture_count(1); state.leaveCount = fixture_count(2);
            if (Time.realtimeSinceStartup >= nextWrite) { System.IO.File.WriteAllText(statePath, JsonUtility.ToJson(state)); nextWrite = Time.realtimeSinceStartup + 0.1f; }
            if (state.hostCount == 1 && state.joinCount == 1 && state.leaveCount == 1) break;
            yield return null;
        }
        System.IO.File.WriteAllText(statePath, JsonUtility.ToJson(state));
        Check("os_callbacks_once", state.hostCount == 1 && state.joinCount == 1 && state.leaveCount == 1);
        bool edited = Environment.GetEnvironmentVariable("WSM_MENU_EDIT_INPUT_TEST") == "1";
        Check("os_callback_values", fixture_port() == (edited ? 8123 : 7780) && fixture_address_is(edited ? "192.0.2.7" : "127.0.0.1") == 1);
        yield return new WaitForEndOfFrame();
        var screenshot = Environment.GetEnvironmentVariable("WSM_MENU_SCREENSHOT");
        if (!string.IsNullOrEmpty(screenshot)) ScreenCapture.CaptureScreenshot(screenshot);
        for (int i = 0; i < 20; ++i) yield return null;
        Check("os_screenshot_written", !string.IsNullOrEmpty(screenshot) && System.IO.File.Exists(screenshot));
    }
    void Update() { if (Time.frameCount > 10000) { Debug.LogError("MENU_FIXTURE timeout"); Application.Quit(2); } }
    void Finish() { Debug.Log("MENU_FIXTURE RESULT failures=" + failures); Application.Quit(failures == 0 ? 0 : 1); }
}
