# VR diagnostic controls

Normal Release gameplay defaults to no passive diagnostic logging, counters,
observation hooks or automatic image readbacks. These settings are read once
at startup; restart the game to change them.

```ini
[DEVELOPMENT]
DiagnosticLogging=0
DiagnosticCounters=0
DiagnosticObservationHooks=0
DiagnosticAutomaticCaptures=0

[VR]
ShowFirstPersonBody=1
```

| Setting | Enables |
| --- | --- |
| DiagnosticLogging | Stereo/OpenXR/D3D9Ex log files and debugger output, including periodic frame messages. |
| DiagnosticCounters | Passive hand, physics, weapon, HUD, audio and visibility counters and diagnostic snapshots. |
| DiagnosticObservationHooks | Bullet-hit health/damage, ragdoll-deactivation and movement/velocity observation hooks. Enable counters too to collect their passive measurements. |
| DiagnosticAutomaticCaptures | Startup/device-reset eye, depth, lighting and shader readbacks. Explicit capture commands still work when this is off. |
| ShowFirstPersonBody | First-person body with head hidden; defaults on independently of hand IK. The old DebugFirstPersonBody key is obsolete. |

`tools/game-command.ps1 diagnostics-status` reports the loaded diagnostic flags.
With counters disabled, passive counter/snapshot fields in other status commands
remain zero or at their initial values; they do not prove a gameplay feature is
inactive. Resource readiness, tracking/gameplay state, ammo and safety information
remain available where they are already needed by the implementation.

Explicit `profile-start*`, capture, omission and other development commands remain
available. Existing Probe settings and trace-enable marker files remain explicit
opt-ins; leave those off/absent for ordinary play. Profiling spans still perform
an inactive-capture flag check; timing reads, sample storage and GPU queries are
only performed during an explicitly started capture. Ordinary status strings
are formatted only on request.

Collision filtering, ragdoll-settling prevention, native reload suppression,
tracked pose publication, shader corrections, model lifetime retention and
command-buffer safety checks are gameplay requirements and are not disabled by
these diagnostic settings. Frame counters used for scheduling and bounded tests
also remain active. No FPS improvement is claimed without measurement.
