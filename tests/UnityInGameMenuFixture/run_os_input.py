"""Exercise the owned window only; OS input is sent only after foreground/PID checks."""
import ctypes
from ctypes import wintypes
import json
import os
import subprocess
import sys
import time
import uuid

u = ctypes.WinDLL("user32", use_last_error=True)
try:
    u.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
except AttributeError:
    u.SetProcessDPIAware()
HWND = wintypes.HWND
u.GetForegroundWindow.restype = HWND
u.GetWindowThreadProcessId.argtypes = [HWND, ctypes.POINTER(wintypes.DWORD)]
u.GetClassNameW.argtypes = [HWND, wintypes.LPWSTR, ctypes.c_int]
u.GetClientRect.argtypes = [HWND, ctypes.POINTER(wintypes.RECT)]
u.ClientToScreen.argtypes = [HWND, ctypes.POINTER(wintypes.POINT)]
u.PostMessageW.argtypes = [HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
u.SetForegroundWindow.argtypes = [HWND]
u.BringWindowToTop.argtypes = [HWND]
u.AttachThreadInput.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.BOOL]
kernel = ctypes.WinDLL("kernel32", use_last_error=True)
kernel.GetCurrentThreadId.restype = wintypes.DWORD
u.SetCursorPos.argtypes = [ctypes.c_int, ctypes.c_int]
u.GetCursorPos.argtypes = [ctypes.POINTER(wintypes.POINT)]
CALLBACK = ctypes.WINFUNCTYPE(wintypes.BOOL, HWND, wintypes.LPARAM)
u.EnumWindows.argtypes = [CALLBACK, wintypes.LPARAM]

class MouseInput(ctypes.Structure):
    _fields_ = [("dx", wintypes.LONG), ("dy", wintypes.LONG), ("mouseData", wintypes.DWORD), ("dwFlags", wintypes.DWORD), ("time", wintypes.DWORD), ("dwExtraInfo", ctypes.c_size_t)]
class KeyboardInput(ctypes.Structure):
    _fields_ = [("wVk", wintypes.WORD), ("wScan", wintypes.WORD), ("dwFlags", wintypes.DWORD), ("time", wintypes.DWORD), ("dwExtraInfo", ctypes.c_size_t)]
class HardwareInput(ctypes.Structure):
    _fields_ = [("uMsg", wintypes.DWORD), ("wParamL", wintypes.WORD), ("wParamH", wintypes.WORD)]
class InputUnion(ctypes.Union):
    _fields_ = [("mi", MouseInput), ("ki", KeyboardInput), ("hi", HardwareInput)]
class Input(ctypes.Structure):
    _fields_ = [("type", wintypes.DWORD), ("union", InputUnion)]
u.SendInput.argtypes = [wintypes.UINT, ctypes.POINTER(Input), ctypes.c_int]
u.SendInput.restype = wintypes.UINT

def mouse(flags):
    item = Input(0, InputUnion(mi=MouseInput(0, 0, 0, flags, 0, 0)))
    if u.SendInput(1, ctypes.byref(item), ctypes.sizeof(item)) != 1:
        raise OSError(ctypes.get_last_error(), "SendInput failed")

def key(vk, flags=0):
    item = Input(1, InputUnion(ki=KeyboardInput(vk, 0, flags, 0, 0)))
    if u.SendInput(1, ctypes.byref(item), ctypes.sizeof(item)) != 1:
        raise OSError(ctypes.get_last_error(), "SendInput keyboard failed")

def wait_for(fn, timeout, what):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        value = fn()
        if value:
            return value
        if proc.poll() is not None:
            raise RuntimeError("fixture exited before " + what + ": " + str(proc.returncode))
        time.sleep(0.05)
    raise TimeoutError(what)

def get_state():
    try:
        with open(state_path, encoding="utf-8") as stream:
            return json.load(stream)
    except (OSError, ValueError):
        return None

def window():
    found = []
    @CALLBACK
    def visitor(hwnd, _):
        pid = wintypes.DWORD()
        u.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        name = ctypes.create_unicode_buffer(128)
        if pid.value == proc.pid and u.GetClassNameW(hwnd, name, 128) and name.value == "UnityWndClass":
            found.append(hwnd)
            return False
        return True
    u.EnumWindows(visitor, 0)
    return found[0] if found else None

def position(hwnd, state, which):
    rect = wintypes.RECT()
    origin = wintypes.POINT(0, 0)
    if not u.GetClientRect(hwnd, ctypes.byref(rect)) or not u.ClientToScreen(hwnd, ctypes.byref(origin)):
        raise OSError(ctypes.get_last_error(), "client geometry")
    width, height = rect.right - rect.left, rect.bottom - rect.top
    if not width or not height or not state["width"] or not state["height"]:
        raise ValueError("empty geometry")
    x = round(state[which + "X"] * width / state["width"])
    y = round((state["height"] - state[which + "Y"]) * height / state["height"])
    if not (0 <= x < width and 0 <= y < height):
        raise ValueError("button outside client area")
    return x, y, origin.x + x, origin.y + y

root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
local = os.path.join(root, ".local")
exe = os.path.join(local, "ingame-ui-v1-player", "Fixture.exe")
quick = "--quick-click-regression" in sys.argv
edit = "--edit-fields" in sys.argv
modal = "--modal" in sys.argv
stem = "ingame-ui-v1-quick" if quick else "ingame-ui-v1-os"
state_path = os.path.join(local, stem + "-state.json")
image_path = os.path.join(local, stem + ".png")
for path in (state_path, image_path):
    if os.path.exists(path): os.remove(path)
env = os.environ.copy()
env["WSM_MENU_STATE"] = state_path
env["WSM_MENU_SCREENSHOT"] = image_path
if edit: env["WSM_MENU_EDIT_INPUT_TEST"] = "1"
if modal: env["WSM_MENU_MODAL"] = "1"
if "--no-event-system" in sys.argv: env["WSM_MENU_NO_EVENT_SYSTEM"] = "1"
if "--keyboard-only" in sys.argv: env["WSM_MENU_KEYBOARD_ONLY"] = "1"
if "--steal-selection" in sys.argv: env["WSM_MENU_STEAL_SELECTION"] = "1"
if "--force-selection" in sys.argv: env["WSM_MENU_FORCE_SELECTION"] = "1"
tick_event = None
if quick:
    kernel.CreateEventW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR]
    kernel.CreateEventW.restype = wintypes.HANDLE
    kernel.ResetEvent.argtypes = [wintypes.HANDLE]
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    event_name = "Local\\WSMMenuTick-" + uuid.uuid4().hex
    tick_event = kernel.CreateEventW(None, False, False, event_name)
    if not tick_event: raise OSError(ctypes.get_last_error(), "CreateEvent")
    env["WSM_MENU_QUICK_INPUT_TEST"] = "1"
    env["WSM_MENU_TICK_EVENT"] = event_name
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 4
args = [exe, "--os-input-test", "-screen-fullscreen", "0", "-screen-width", "1280", "-screen-height", "720", "-logFile", os.path.join(local, stem + ".log")]
proc = subprocess.Popen(args, env=env, startupinfo=startup)
old_cursor = wintypes.POINT()
u.GetCursorPos(ctypes.byref(old_cursor))
try:
    hwnd = wait_for(window, 20, "own UnityWndClass")
    state = wait_for(get_state, 20, "fresh Unity geometry")
    print("OWN_WINDOW pid=%d hwnd=%s screen=%dx%d" % (proc.pid, hwnd, state["width"], state["height"]), flush=True)
    if u.GetForegroundWindow() != hwnd:
        x, y, _, _ = position(hwnd, state, "host")
        lp = (y << 16) | (x & 0xffff)
        for msg, wp in ((0x0200, 0), (0x0201, 1), (0x0202, 0)):
            if not u.PostMessageW(hwnd, msg, wp, lp): raise OSError(ctypes.get_last_error(), "PostMessage failed")
        time.sleep(0.5)
        after = get_state()
        if not after or after["hostCount"] != 0: raise RuntimeError("background click unexpectedly invoked callback")
        print("BACKGROUND_POST_ONCE=no-op verified", flush=True)
    else:
        print("BACKGROUND_POST_ONCE=skipped (fixture already foreground)", flush=True)
    if not u.SetForegroundWindow(hwnd):
        current = kernel.GetCurrentThreadId()
        foreground_window = u.GetForegroundWindow()
        foreground_thread = u.GetWindowThreadProcessId(foreground_window, None) if foreground_window else 0
        if not foreground_thread or not u.AttachThreadInput(current, foreground_thread, True):
            raise RuntimeError("foreground permission refused; no input sent")
        try:
            u.BringWindowToTop(hwnd)
            u.SetForegroundWindow(hwnd)
        finally:
            u.AttachThreadInput(current, foreground_thread, False)
    wait_for(lambda: u.GetForegroundWindow() == hwnd, 45 if "--external-focus" in sys.argv else 3, "fixture foreground")
    time.sleep(0.3)  # Allow tick() to observe the new foreground before a press.
    if modal:
        state = wait_for(get_state, 3, "modal geometry")
        if not state["modalTopmost"]: raise RuntimeError("modal fixture is not topmost in EventSystem.RaycastAll")
        _, _, sx, sy = position(hwnd, state, "host")
        if u.GetForegroundWindow() != hwnd: raise RuntimeError("foreground changed before modal click")
        if not u.SetCursorPos(sx, sy): raise OSError(ctypes.get_last_error(), "SetCursorPos")
        mouse(0x0002); time.sleep(.08); mouse(0x0004); time.sleep(.8)
        if get_state()["hostCount"] != 0: raise AssertionError("higher modal click invoked underlying Host")
        print("HIGHER_MODAL_BLOCKS_HOST=PASS", flush=True)
        sys.exit(0)
    if quick:
        wait_for(lambda: (s if (s := get_state()) and s["panelVisible"] else None), 3, "visible panel")
        for visible in (False, True):
            if u.GetForegroundWindow() != hwnd: raise RuntimeError("foreground changed before F6")
            if not kernel.ResetEvent(tick_event): raise OSError(ctypes.get_last_error(), "ResetEvent")
            if kernel.WaitForSingleObject(tick_event, 3000) != 0: raise TimeoutError("native tick acknowledgement")
            key(0x75)
            try: time.sleep(0.020)
            finally: key(0x75, 0x0002)
            wait_for(lambda: (s if (s := get_state()) and s["panelVisible"] == visible else None), 3, "short F6 panel=" + str(visible))
        print("SENDINPUT_SHORT_F6=PASS", flush=True)
        if u.GetForegroundWindow() != hwnd: raise RuntimeError("foreground changed before held F6")
        key(0x75)
        try:
            wait_for(lambda: (s if (s := get_state()) and not s["panelVisible"] else None), 3, "held F6 hides")
            # SendInput does not itself guarantee Windows auto-repeat; inject a
            # repeat-marked message to this exact owned foreground window.
            if not u.PostMessageW(hwnd, 0x0100, 0x75, (1 << 30) | 1):
                raise OSError(ctypes.get_last_error(), "PostMessage F6 repeat failed")
            time.sleep(1.2)
            if u.GetForegroundWindow() != hwnd: raise RuntimeError("foreground changed during held F6")
            if get_state()["panelVisible"]: raise RuntimeError("held F6 repeated")
        finally: key(0x75, 2)
        time.sleep(.1)
        key(0x75); time.sleep(.02); key(0x75, 2)
        wait_for(lambda: (s if (s := get_state()) and s["panelVisible"] else None), 3, "F6 restores after hold")
        print("SENDINPUT_HELD_F6_NO_REPEAT=PASS", flush=True)
        state = wait_for(get_state, 3, "drag geometry")
        _, _, sx, sy = position(hwnd, state, "host")
        _, _, ex, ey = position(hwnd, state, "join")
        if u.GetForegroundWindow() != hwnd: raise RuntimeError("foreground changed before drag")
        u.SetCursorPos(sx, sy); mouse(0x0002); time.sleep(.05)
        u.SetCursorPos(ex, ey); time.sleep(.05); mouse(0x0004); time.sleep(.5)
        state = get_state()
        if state["hostCount"] or state["joinCount"]: raise AssertionError("drag to a different button activated callback")
        print("SENDINPUT_DRAG_CANCEL=PASS", flush=True)
    if edit:
        for field, value in (("address", "192.0.2.7"), ("port", "8123")):
            state = wait_for(get_state, 3, "field geometry")
            _, _, sx, sy = position(hwnd, state, field)
            if u.GetForegroundWindow() != hwnd: raise RuntimeError("foreground changed before field click")
            if not u.SetCursorPos(sx, sy): raise OSError(ctypes.get_last_error(), "SetCursorPos")
            mouse(0x0002); time.sleep(.08); mouse(0x0004); time.sleep(.3)
            if u.GetForegroundWindow() != hwnd: raise RuntimeError("foreground changed before field typing")
            key(0x11); key(0x41); key(0x41, 2); key(0x11, 2)
            for char in value:
                vk = 0xBE if char == "." else ord(char)
                key(vk); key(vk, 2); time.sleep(.04)
            time.sleep(.3)
    for which in ("host", "join", "leave"):
        state = wait_for(get_state, 3, "geometry")
        _, _, sx, sy = position(hwnd, state, which)
        if u.GetForegroundWindow() != hwnd: raise RuntimeError("foreground changed; stopping")
        if not u.SetCursorPos(sx, sy): raise OSError(ctypes.get_last_error(), "SetCursorPos")
        time.sleep(0.12)
        if u.GetForegroundWindow() != hwnd: raise RuntimeError("foreground changed before press")
        if quick:
            if not kernel.ResetEvent(tick_event): raise OSError(ctypes.get_last_error(), "ResetEvent")
            if kernel.WaitForSingleObject(tick_event, 3000) != 0: raise TimeoutError("native tick acknowledgement")
        mouse(0x0002)
        try: time.sleep(0.020 if quick else 0.20)
        finally: mouse(0x0004)
        expected = which + "Count"
        wait_for(lambda: (s if (s := get_state()) and s[expected] == 1 else None), 5, which + " callback")
        print("SENDINPUT_" + which.upper() + "=PASS", flush=True)
    code = proc.wait(timeout=15)
    print("OS_FIXTURE_EXIT=" + str(code), flush=True)
    sys.exit(code)
finally:
    if proc.poll() is None:
        proc.terminate()
        try: proc.wait(timeout=5)
        except subprocess.TimeoutExpired: proc.kill(); proc.wait()
    u.SetCursorPos(old_cursor.x, old_cursor.y)
    if tick_event: kernel.CloseHandle(tick_event)
