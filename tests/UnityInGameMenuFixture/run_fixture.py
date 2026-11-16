"""Run only the original owned fixture without requesting foreground activation."""
import os
import subprocess
import sys

root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
local = os.path.join(root, ".local")
player = os.environ.get("WSM_MENU_PLAYER", os.path.join(local, "ingame-ui-v1-player"))
exe = os.path.join(player, "Fixture.exe")
run = sys.argv[1] if len(sys.argv) > 1 else "positive"
env = os.environ.copy()
env["WSM_MENU_SCREENSHOT"] = os.path.join(local, "ingame-ui-v1-" + run + ".png")
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 4  # SW_SHOWNOACTIVATE
args = [exe, "-screen-fullscreen", "0", "-screen-width", "1280", "-screen-height", "720", "-logFile", os.path.join(local, "ingame-ui-v1-" + run + ".log")]
if run == "forced-negative":
    args.append("--force-failure")
result = subprocess.run(args, env=env, startupinfo=startup, timeout=120)
print("FIXTURE_EXIT=" + str(result.returncode))
sys.exit(result.returncode)
