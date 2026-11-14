using System;
using System.Runtime.InteropServices;
using UnityEngine;

// IL2CPP coexistence proof: the Unity player calls the published Native AOT DLL directly.
// The MonoBehaviour hosts a real TCP session inside the game process and waits for an
// external guest to join, so both the interop path and the transport are exercised.
public sealed class AotRuntimeFixture : MonoBehaviour
{
    [DllImport("Wsm.InGame.Runtime", CallingConvention = CallingConvention.Cdecl)]
    private static extern int wsm_ingame_version();
    [DllImport("Wsm.InGame.Runtime", CallingConvention = CallingConvention.Cdecl)]
    private static extern int wsm_ingame_host(int port, int lanBind);
    [DllImport("Wsm.InGame.Runtime", CallingConvention = CallingConvention.Cdecl)]
    private static extern int wsm_ingame_status(IntPtr buffer, int capacity);
    [DllImport("Wsm.InGame.Runtime", CallingConvention = CallingConvention.Cdecl)]
    private static extern int wsm_ingame_state();
    [DllImport("Wsm.InGame.Runtime", CallingConvention = CallingConvention.Cdecl)]
    private static extern int wsm_ingame_peer_count();
    [DllImport("Wsm.InGame.Runtime", CallingConvention = CallingConvention.Cdecl)]
    private static extern int wsm_ingame_bound_port();
    [DllImport("Wsm.InGame.Runtime", CallingConvention = CallingConvention.Cdecl)]
    private static extern int wsm_ingame_leave();

    private const int StateHosting = 1;
    private float deadline;
    private bool started;
    private bool hosted;
    private bool peerSeen;
    private float finishAt;
    private bool finished;

    private static string Status()
    {
        var required = wsm_ingame_status(IntPtr.Zero, 0);
        if (required < 0) return "<error " + required + ">";
        var buffer = Marshal.AllocHGlobal(required + 1);
        try
        {
            return wsm_ingame_status(buffer, required + 1) == required
                ? Marshal.PtrToStringUTF8(buffer) ?? "<null>"
                : "<write mismatch>";
        }
        finally { Marshal.FreeHGlobal(buffer); }
    }

    private void Start()
    {
        Debug.Log("GAME_RUNTIME VERSION=" + wsm_ingame_version());
        Debug.Log("GAME_RUNTIME IDLE_STATUS=" + Status());
        Debug.Log("GAME_RUNTIME HOST_RC=" + wsm_ingame_host(0, 0));
        deadline = Time.realtimeSinceStartup + 45f;
        started = true;
    }

    private void Update()
    {
        if (!started || finished) return;
        if (!hosted && wsm_ingame_state() == StateHosting)
        {
            hosted = true;
            Debug.Log("GAME_RUNTIME HOSTED_PORT=" + wsm_ingame_bound_port());
            Debug.Log("GAME_RUNTIME HOSTED_STATUS=" + Status());
        }
        if (wsm_ingame_peer_count() == 1)
        {
            if (!peerSeen)
            {
                peerSeen = true;
                Debug.Log("GAME_RUNTIME PEERS=" + wsm_ingame_peer_count());
                Debug.Log("GAME_RUNTIME PEER_STATUS=" + Status());
                // Hold briefly so the external guest also observes the connected state.
                finishAt = Time.realtimeSinceStartup + 2f;
            }
        }
        // The hold is time-based: the guest may already have left, which must not mask the result.
        if (peerSeen && Time.realtimeSinceStartup >= finishAt)
        {
            Finish(true);
            return;
        }
        if (Time.realtimeSinceStartup > deadline)
        {
            Debug.Log("GAME_RUNTIME TIMEOUT_STATE=" + wsm_ingame_state());
            Finish(false);
        }
    }

    private void Finish(bool ok)
    {
        finished = true;
        Debug.Log("GAME_RUNTIME LEAVE_RC=" + wsm_ingame_leave());
        Debug.Log(ok ? "GAME_RUNTIME RESULT=PASS" : "GAME_RUNTIME RESULT=FAIL");
        Application.Quit(ok ? 0 : 3);
    }
}
