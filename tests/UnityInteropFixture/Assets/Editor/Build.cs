using UnityEditor; using UnityEditor.Build; using UnityEditor.SceneManagement; using UnityEngine; using UnityEngine.SceneManagement;
public static class BuildFixture {
 public static void Build() {
  PlayerSettings.SetScriptingBackend(NamedBuildTarget.Standalone, ScriptingImplementation.IL2CPP);
  PlayerSettings.SetApiCompatibilityLevel(NamedBuildTarget.Standalone, ApiCompatibilityLevel.NET_Standard);
  PlayerSettings.SetManagedStrippingLevel(NamedBuildTarget.Standalone, ManagedStrippingLevel.Low);
  var s=EditorSceneManager.NewScene(NewSceneSetup.DefaultGameObjects,NewSceneMode.Single);
  new GameObject("InteropFixture").AddComponent<Fixture>();
  EditorSceneManager.SaveScene(s,"Assets/Fixture.unity");
  var o=new BuildPlayerOptions { scenes=new[]{"Assets/Fixture.unity"}, locationPathName=System.IO.Path.GetFullPath("../fixture-v3-player/Fixture.exe"), target=BuildTarget.StandaloneWindows64, options=BuildOptions.Development };
  var r=BuildPipeline.BuildPlayer(o); Debug.Log("FIXTURE_BUILD_RESULT="+r.summary.result); if(r.summary.result!=UnityEditor.Build.Reporting.BuildResult.Succeeded) EditorApplication.Exit(2);
 }
}