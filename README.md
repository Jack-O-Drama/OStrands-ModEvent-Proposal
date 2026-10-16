# OStrands — external ModEvent control

Thanks for taking a look! These are the changes behind my local prototype. They let my scripts request player dripping outside scenes, while arousal thresholds, clothing checks and the MCM stay entirely in my add-on.

The prototype produced the effect in game. I'd like to use your official DLL for the released integration. Feel free to change the names or implementation; I'll adapt my scripts to your API.

## Requests

Event: `EBF_OStrands_Request`, sent from `Game.GetPlayer()`.

| Command (`strArg`) | Action |
| --- | --- |
| `ping` | Return status without starting the effect |
| `start` | Start or renew the request; `numArg` is the interval in seconds, clamped to 1–30 |
| `stop` | Cancel the request and remove externally tagged strands |

Example calls, used separately as needed:

```papyrus
Game.GetPlayer().SendModEvent("EBF_OStrands_Request", "ping", 0.0)
Game.GetPlayer().SendModEvent("EBF_OStrands_Request", "start", 4.0)
Game.GetPlayer().SendModEvent("EBF_OStrands_Request", "stop", 0.0)
```

My controller renews `start` every two seconds while its conditions are met. The request expires after eight seconds of unpaused update time without renewal. Renewing does not reset the interval between emission attempts. A non-positive or non-finite interval acts as `stop`.

## Status

Response event: `EBF_OStrands_Status`. `strArg` contains the status, `numArg` is API version `1.0`, and the sender is the player. Responses are sent on `ping` and when the status changes.

```papyrus
RegisterForModEvent("EBF_OStrands_Status", "OnExternalStatus")

Event OnExternalStatus(String eventName, String status, Float apiVersion, Form sender)
EndEvent
```

Statuses: `idle`, `active`, `body`, `scene`, `linger`, `disabled`, `mesh`, `expired`. `active` means eligible for emission; existing limits can still prevent a visible strand. A ping may report the previous frame's state.

## Native behavior

- Player-only, using the existing vaginal anatomy nodes.
- Suppresses external emission during tracked scenes and lingering effects, or when the required body is unavailable.
- Tags external strands so stopping them does not intentionally clear scene-owned effects.
- Respects OStrands settings and limits; does not read or change arousal.
- Queues commands in the event listener and handles them in the frame-update path.
- Resets requests on load/new game. No request state is serialized.
- Compares request names and commands case-insensitively to tolerate Papyrus string capitalization.

This prototype has one shared external channel, not per-mod ownership. The latest start/stop wins, so multiple controllers would need arbitration. Scene suppression relies on the existing bridge notifications.

## Files

- `patches/0001-external-drips-modevents.patch` — native changes.
- `changed-files/` — complete versions of the affected files for reading.
- `examples/OStrandsExternalExample.psc` — minimal caller with start, stop, status and renewal (illustrative, not game-tested).
- `LICENSE` — GPL-3.0, retained for this OStrands-derived code.

Based on OStrands commit `4067df72a4b1fcec38e6f91265e4438c749c3e8e`. OStrands remains the work of its original authors; the added code is marked in the source.
