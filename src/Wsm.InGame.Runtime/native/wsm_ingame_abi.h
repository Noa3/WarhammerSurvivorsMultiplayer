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
#define WSM_INGAME_GAMEPLAY_VERSION 1
#define WSM_INGAME_GAMEPLAY_REQUEST_MAX_BYTES 262144
#define WSM_INGAME_GAMEPLAY_EVENT_MAX_BYTES 524288

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
// Optional atomic host command: bots 0..8, difficulty easy/normal/hard = 0/1/2,
// death all-dead/first-death = 0/1. Defaults of the frozen host call: 0,1,0.
int wsm_ingame_host_rules(int port, int lanBind, int bots, int difficulty, int death);
int wsm_ingame_join(const uint8_t* ipUtf8, int ipLength, int port);
int wsm_ingame_leave(void);
int wsm_ingame_status(uint8_t* buffer, int capacity);
// Optional, status buffer convention. Up to two local IPv4:port candidates separated by ", ".
// Empty unless hosting on LAN with a suitable interface; never sent to peers.
int wsm_ingame_lan_hint(uint8_t* buffer, int capacity);
// Optional display names: strict UTF-8 input, sanitized and frozen at queue time.
// Getters use the status buffer convention; roster is host-first, newline-separated,
// has no trailing newline, and is empty when disconnected. Names are not identity.
int wsm_ingame_set_player_name(const uint8_t* nameUtf8, int length);
int wsm_ingame_local_player_name(uint8_t* buffer, int capacity);
int wsm_ingame_player_names(uint8_t* buffer, int capacity);
int wsm_ingame_last_error(uint8_t* buffer, int capacity);
int wsm_ingame_state(void);
int wsm_ingame_peer_count(void);
int wsm_ingame_bound_port(void);
int wsm_ingame_shutdown(void);

// Optional gameplay transport, separate from engine integration. Never infer
// that an accepted request has changed Unity or been received by another peer.
// info uses the status buffer convention and reports epoch/role/playerId/active/error.
// submit accepts camelCase JSON with kind and positive requestId. Guests may
// send input{x,y} or upgrade{level,choice}; identity is assigned by the session.
// Hosts may send snapshot{snapshot:{tick,entities,enemies:[]}} or reward{reward:...}.
// Source builds supporting botFrame also accept host-local bot observations. In that
// case submission.botDecision is an intent for the supplied run/slot/tick/actor,
// not an applied movement, hit, collection or upgrade. See ../README.md. Older
// libraries reject this request kind; do not fall back to simulated positions.
// Payload fields are flattened at the request root, except snapshot and reward.
// poll(NULL,0) queries the next event length, 0 means none. Sufficient capacity
// copies UTF-8+NUL and consumes it; -5 consumes nothing and leaves bytes alone.
// Events are camelCase JSON with epoch, eventId, kind, playerId, requestId and
// ageMilliseconds; nullable fields may be absent. Kinds: joined, left, input,
// upgrade, snapshot, reward, submission, undelivered, ended. Corresponding payloads are in
// input/upgrade/snapshot/reward; submission carries the original requestId and
// optional error (including "cancelled"), absent error means local send succeeded,
// not remote application. ended carries error; non-submission requestId is 0.
// Event IDs represent creation order, but an ended event can be delivered after
// later-created reliable submissions/events. Join precedes that player's commands
// and leave; terminal delivery waits for retained reliable events and submissions.
// On host-reader failure, admitted Core joins are reconciled after producers stop.
// A held upgrade without available join provenance is retained as kind undelivered,
// with its original playerId/upgrade and error join_unconfirmed_at_session_end.
// It is not a normal applicable upgrade and does not fabricate a joined event.
// Consume this failure before reconnect; terminal follows it. Coalescible held
// movement is discarded at teardown. Shutdown spill is capped by 512 pre-join slots.
// Input ageMilliseconds is receiver-clock age: reject stale movement before
// creating a local lease. Movement and snapshots coalesce, rewards do not.
// Submit returns -3 when its 32-slot outgoing queue or 512 pending-result slots
// are full; rejected requests are not reserved. Duplicate pending IDs return -1.
// Up to 512 non-lifecycle control events and 512 pre-join events are buffered;
// overflow can terminate the gameplay session, not silently drop choices. Lifecycle
// backlog is bounded by 1024 known and 100000 departed player identities; it is
// not capped at 512 and retains join/leave. The 100000 receipt ledger is bounded.
// Core queues one notification per accepted unique ledger entry, up to the same
// session limit. A guest pausing its consumer does not drop these notifications.
// A socket loss can still interrupt rewards before they reach the guest.
// Leave explicitly abandons queued data; host loss retains reliable events.
// A new connection is refused while unconsumed retained reliable events exist.
// Native callers must poll on their own bounded main-thread handoff, not call
// Unity from any network task. No frame/update timing guarantee is made here.
int wsm_ingame_gameplay_version(void);
int wsm_ingame_gameplay_epoch(void);
int wsm_ingame_gameplay_info(uint8_t* buffer, int capacity);
int wsm_ingame_gameplay_submit(int expectedEpoch, const uint8_t* json, int length);
int wsm_ingame_gameplay_poll(uint8_t* buffer, int capacity);

#ifdef __cplusplus
}
#endif
