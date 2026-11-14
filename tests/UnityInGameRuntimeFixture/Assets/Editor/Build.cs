using UnityEditor;
using UnityEditor.Build;
using UnityEditor.SceneManagement;
using UnityEditor.Build.Reporting;
using UnityEngine;
using UnityEngine.SceneManagement;

public static class BuildRuntimeFixture
{
    public static void Build()
    {
        PlayerSettings.SetScriptingBackend(NamedBuildTarget.Standalone, ScriptingImplementation.IL2CPP);
        PlayerSettings.SetApiCompatibilityLevel(NamedBuildTarget.Standalone, ApiCompatibilityLevel.NET_Standard);
        PlayerSettings.SetManagedStrippingLevel(NamedBuildTarget.Standalone, ManagedStrippingLevel.Low);
        PlayerSettings.defaultScreenWidth = 1024;
        PlayerSettings.defaultScreenHeight = 768;
        PlayerSettings.runInBackground = true;
        var scene = EditorSceneManager.NewScene(NewSceneSetup.DefaultGameObjects, NewSceneMode.Single);
        new GameObject("AotRuntimeFixture").AddComponent<AotRuntimeFixture>();
        EditorSceneManager.SaveScene(scene, "Assets/Fixture.unity");
        var options = new BuildPlayerOptions
        {
            scenes = new[] { "Assets/Fixture.unity" },
            locationPathName = System.IO.Path.GetFullPath(System.Environment.GetEnvironmentVariable("WSM_RUNTIME_OUTPUT")),
            target = BuildTarget.StandaloneWindows64,
            options = BuildOptions.Development,
        };
        var result = BuildPipeline.BuildPlayer(options);
        Debug.Log("RUNTIME_FIXTURE_BUILD_RESULT=" + result.summary.result);
        if (result.summary.result != BuildResult.Succeeded) EditorApplication.Exit(2);
    }
}
