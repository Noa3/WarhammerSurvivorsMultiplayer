"""Build and run original pump or controller-driver IL2CPP fixtures without private caches.

Requires Windows x64, CMake/Visual Studio C++, and the project's Unity IL2CPP editor.
This tests production pump ownership with original event/client doubles, not game timing.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import uuid


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def sources(root, project):
    paths = {p for folder in (root / "native/include", root / "native/src", project / "Native")
             for p in folder.rglob("*") if p.is_file() and p.suffix in (".h", ".cpp", ".txt")}
    paths.update(p for folder in ("Assets", "Packages", "ProjectSettings")
                 for p in (project / folder).rglob("*") if p.is_file())
    paths.update((Path(__file__).resolve(), project / "run_input_pump_fixture.py", project / "run_input_driver_fixture.py"))
    return {p.relative_to(root).as_posix(): digest(p) for p in sorted(paths)}


def main():
    root = Path(__file__).resolve().parents[2]
    project = Path(__file__).resolve().parent
    version_lines = (project / "ProjectSettings/ProjectVersion.txt").read_text().splitlines()
    version = next(line.removeprefix("m_EditorVersion: ") for line in version_lines
                   if line.startswith("m_EditorVersion: "))
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", choices=("pump", "driver"), default="pump")
    parser.add_argument("--unity", type=Path, default=Path("C:/Program Files/Unity/Hub/Editor") / version / "Editor/Unity.exe")
    parser.add_argument("--output", type=Path, help="New, nonexistent directory for this complete run")
    args = parser.parse_args()
    entry = "InputPump" if args.suite == "pump" else "InputDriver"
    runner = project / ("run_input_pump_fixture.py" if args.suite == "pump" else "run_input_driver_fixture.py")
    if sys.platform != "win32" or not args.unity.is_file() or not shutil.which("cmake"):
        parser.error("Windows, CMake and the specified Unity IL2CPP editor are required")
    output = (args.output or root / ".local/input-pump-fixtures" / uuid.uuid4().hex).resolve()
    if output.exists():
        parser.error("Output directory already exists; preserve it and choose a new run")
    output.mkdir(parents=True)
    receipt = {"startedUtc": datetime.now(timezone.utc).isoformat(), "accepted": False, "suite": args.suite,
               "sourceHashes": sources(root, project), "steps": [], "targetGameExecuted": False,
               "vendorRewiredExecuted": False, "output": str(output)}
    report = output / "build-result.json"
    env = os.environ.copy()
    # Respect the caller's owned temporary workspace. Do not change global environment.
    if env.get("TMPDIR"):
        env["TMP"] = env["TEMP"] = env["TMPDIR"]

    def save():
        report.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")

    def run(name, command, timeout):
        step = {"name": name, "command": [str(x) for x in command], "exitCode": None}
        receipt["steps"].append(step)
        save()
        print("INPUT_PUMP_BUILD_STEP", name, flush=True)
        with (output / (name + ".log")).open("w", encoding="utf-8") as log:
            result = subprocess.run(step["command"], cwd=root, env=env, stdout=log,
                                    stderr=subprocess.STDOUT, timeout=timeout)
        step["exitCode"] = result.returncode
        save()
        if result.returncode != 0:
            raise RuntimeError(name + " failed; see " + str(output / (name + ".log")))

    save()
    try:
        native = output / "native"
        run("native-configure", ["cmake", "-S", project / "Native", "-B", native, "-A", "x64"], 120)
        run("native-build", ["cmake", "--build", native, "--config", "Release"], 180)
        unity = output / "unity"
        for folder in ("Assets", "Packages", "ProjectSettings"):
            shutil.copytree(project / folder, unity / folder)
        plugins = unity / "Assets/Plugins/x86_64"
        plugins.mkdir(parents=True, exist_ok=True)
        plugin = native / "Release/fixture.dll"
        shutil.copyfile(plugin, plugins / "fixture.dll")
        player = output / "player"
        env["WSM_INPUT_PUMP_OUTPUT" if args.suite == "pump" else "WSM_INPUT_DRIVER_OUTPUT"] = str(player / "Fixture.exe")
        editor_log = output / "editor.log"
        run("unity-build", [args.unity.resolve(), "-batchmode", "-nographics", "-quit", "-projectPath", unity,
                            "-executeMethod", "Build" + entry + "Fixture.Build", "-logFile", editor_log], 600)
        marker = "INPUT_PUMP_FIXTURE_BUILD=Succeeded" if args.suite == "pump" else "INPUT_DRIVER_FIXTURE_BUILD=Succeeded"
        if marker not in editor_log.read_text(errors="replace"):
            raise RuntimeError("Unity did not report a successful player build")
        if digest(player / "Fixture_Data/Plugins/x86_64/fixture.dll") != digest(plugin):
            raise RuntimeError("Built player did not contain the expected production fixture plugin")
        run("player-run", [sys.executable, runner, "--player", player,
                           "--output", output / "execution", "--plugin-sha256", digest(plugin)], 170)
        result = json.loads((output / "execution/result.json").read_text())
        receipt["execution"] = result
        receipt["sourceInputsUnchanged"] = receipt["sourceHashes"] == sources(root, project)
        receipt["accepted"] = result["accepted"] and receipt["sourceInputsUnchanged"]
        if not receipt["accepted"]:
            raise RuntimeError("Fixture execution or complete source inventory changed")
        print("INPUT_PUMP_BUILD_RESULT PASS", len(result["rows"]), "actual assertions; player exit", result["playerExit"], flush=True)
        return 0
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        receipt["error"] = str(error)
        print("INPUT_PUMP_BUILD_RESULT FAIL", str(error), file=sys.stderr, flush=True)
        return 1
    finally:
        save()
        print("INPUT_PUMP_BUILD_RECEIPT", report, flush=True)


if __name__ == "__main__":
    sys.exit(main())
