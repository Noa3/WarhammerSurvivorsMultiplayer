using UnityEditor;
using UnityEditor.Build;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;

public static class BuildFixture
{
    public static void Build()
    {
        PlayerSettings.SetScriptingBackend(NamedBuildTarget.Standalone, ScriptingImplementation.IL2CPP);
        PlayerSettings.SetApiCompatibilityLevel(NamedBuildTarget.Standalone, ApiCompatibilityLevel.NET_Standard);
        PlayerSettings.SetManagedStrippingLevel(NamedBuildTarget.Standalone, ManagedStrippingLevel.Low);
        PlayerSettings.defaultScreenWidth = 1024; PlayerSettings.defaultScreenHeight = 768;
        PlayerSettings.runInBackground = true;
        var scene = EditorSceneManager.NewScene(NewSceneSetup.DefaultGameObjects, NewSceneMode.Single);
        Camera.main.backgroundColor = new Color(0.08f, 0.10f, 0.14f);
        Camera.main.clearFlags = CameraClearFlags.SolidColor;
        new GameObject("MenuFixture").AddComponent<MenuFixture>();
        EditorSceneManager.SaveScene(scene, "Assets/Fixture.unity");
        var options = new BuildPlayerOptions { scenes = new[] { "Assets/Fixture.unity" }, locationPathName = System.IO.Path.GetFullPath(System.Environment.GetEnvironmentVariable("WSM_MENU_OUTPUT")), target = BuildTarget.StandaloneWindows64, options = BuildOptions.Development };
        var result = BuildPipeline.BuildPlayer(options);
        Debug.Log("MENU_FIXTURE_BUILD_RESULT=" + result.summary.result);
        if (result.summary.result != UnityEditor.Build.Reporting.BuildResult.Succeeded) EditorApplication.Exit(2);
    }
}
