using UnityEditor;
using UnityEditor.Build;
using UnityEditor.SceneManagement;
using UnityEngine;
public static class BuildSteamFixture {
    public static void Build() {
        PlayerSettings.SetScriptingBackend(NamedBuildTarget.Standalone, ScriptingImplementation.IL2CPP);
        PlayerSettings.SetApiCompatibilityLevel(NamedBuildTarget.Standalone, ApiCompatibilityLevel.NET_Standard);
        PlayerSettings.SetManagedStrippingLevel(NamedBuildTarget.Standalone, ManagedStrippingLevel.Low);
        var scene=EditorSceneManager.NewScene(NewSceneSetup.EmptyScene,NewSceneMode.Single);
        new GameObject("Driver").AddComponent<SteamPersonaFixture>();
        EditorSceneManager.SaveScene(scene,"Assets/Fixture.unity");
        var report=BuildPipeline.BuildPlayer(new BuildPlayerOptions { scenes=new[]{"Assets/Fixture.unity"}, locationPathName=System.IO.Path.GetFullPath(System.Environment.GetEnvironmentVariable("WSM_FIXTURE_OUTPUT")), target=BuildTarget.StandaloneWindows64, options=BuildOptions.None });
        Debug.Log("STEAM_FIXTURE_BUILD="+report.summary.result);
        if(report.summary.result!=UnityEditor.Build.Reporting.BuildResult.Succeeded)EditorApplication.Exit(2);
    }
}
