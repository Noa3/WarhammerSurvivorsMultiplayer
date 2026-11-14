// C ABI of Wsm.InGame.Runtime — a self-contained Native AOT DLL that hosts or joins the
// existing Wsm.Core TCP session inside the game process. Load it once per process; the module
// pins itself and Native AOT libraries cannot be unloaded.
//
// Frozen contract (do not rename):
//   int  wsm_ingame_version(void)                                              -> 1
//   int  wsm_ingame_host(int port, int lanBind)                                -> 0 accepted
//   int  wsm_ingame_join(const uint8_t* ipUtf8, int ipLength, int port)        -> 0 accepted
//   int  wsm_ingame_leave(void)                                                -> 0 accepted
//   int  wsm_ingame_status(uint8_t* buffer, int capacity)                      -> see below
//
// All exports are Cdecl, never throw across the ABI and never block the Unity main thread.
// Extra exports beyond the frozen five: wsm_ingame_last_error, wsm_ingame_state,
// wsm_ingame_peer_count, wsm_ingame_bound_port, wsm_ingame_shutdown.
//
// Command return codes: 0 accepted/queued, -1 invalid argument, -2 unavailable,
// -3 queue full (retry).
// Buffer return codes: required UTF-8 byte length excluding the terminator (>= 0),
// -1 invalid argument (capacity < 0, or null buffer with capacity != 0),
// -2 unavailable, -5 insufficient capacity (buffer left unchanged).
//
// wsm_ingame_status / wsm_ingame_last_error:
//   buffer == NULL, capacity == 0  -> required byte length excluding the terminator
//   buffer != NULL, capacity >= required + 1 -> writes bytes + NUL, returns required length
//   text is capped at 4096 bytes including the terminator and its length changes over time,
//   so query first and re-query after -5.
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WSM_INGAME_ABI_VERSION 1
#define WSM_INGAME_STATUS_MAX_BYTES 4096

#define WSM_INGAME_ACCEPTED 0
#define WSM_INGAME_INVALID_ARGUMENT (-1)
#define WSM_INGAME_UNAVAILABLE (-2)
#define WSM_INGAME_QUEUE_FULL (-3)
#define WSM_INGAME_INSUFFICIENT_CAPACITY (-5)

#define WSM_INGAME_STATE_DISCONNECTED 0
#define WSM_INGAME_STATE_HOSTING 1
#define WSM_INGAME_STATE_CONNECTING 2
#define WSM_INGAME_STATE_CONNECTED 3
#define WSM_INGAME_STATE_FAILED 4

int wsm_ingame_version(void);
int wsm_ingame_host(int port, int lanBind);
int wsm_ingame_join(const uint8_t* ipUtf8, int ipLength, int port);
int wsm_ingame_leave(void);
int wsm_ingame_status(uint8_t* buffer, int capacity);
int wsm_ingame_last_error(uint8_t* buffer, int capacity);
int wsm_ingame_state(void);
int wsm_ingame_peer_count(void);
int wsm_ingame_bound_port(void);
int wsm_ingame_shutdown(void);

#ifdef __cplusplus
}
#endif
