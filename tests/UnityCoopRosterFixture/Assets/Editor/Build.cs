using UnityEditor;
using UnityEditor.Build;
using UnityEditor.SceneManagement;
using UnityEngine;

public static class BuildCoopRosterFixture {
    public static void Build() {
        PlayerSettings.SetScriptingBackend(NamedBuildTarget.Standalone, ScriptingImplementation.IL2CPP);
        PlayerSettings.SetApiCompatibilityLevel(NamedBuildTarget.Standalone, ApiCompatibilityLevel.NET_Standard);
        PlayerSettings.SetManagedStrippingLevel(NamedBuildTarget.Standalone, ManagedStrippingLevel.Low);
        var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
        new GameObject("Driver").AddComponent<CoopRosterFixture>();
        EditorSceneManager.SaveScene(scene, "Assets/Fixture.unity");
        var options = new BuildPlayerOptions {
            scenes = new[] { "Assets/Fixture.unity" },
            locationPathName = System.IO.Path.GetFullPath(System.Environment.GetEnvironmentVariable("WSM_FIXTURE_OUTPUT")),
            target = BuildTarget.StandaloneWindows64,
            options = BuildOptions.None
        };
        var report = BuildPipeline.BuildPlayer(options);
        Debug.Log("COOP_ROSTER_FIXTURE_BUILD=" + report.summary.result);
        if (report.summary.result != UnityEditor.Build.Reporting.BuildResult.Succeeded) EditorApplication.Exit(2);
    }
}
