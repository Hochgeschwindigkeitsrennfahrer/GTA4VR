# Vehicle validation sequence

Tracked under [vehicle interactions #3](https://github.com/bordplate/GTA4VR/issues/3).
The [catalog](vehicle-catalog.csv) contains model identifiers, hashes, native
definition types and display keys derived from the installed CE definitions.
It contains no vehicle assets or copied definition files.

| Episode | Car definitions | Bikes | Boats | Helicopters | Rail/cable | Total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| GTA IV | 105 | 7 | 8 | 4 | 3 | 127 |
| TLAD | 114 | 23 | 8 | 4 | 3 | 152 |
| TBoGT | 124 | 15 | 11 | 7 | 3 | 160 |

These are definition inventories, including special-purpose/non-enterable
models. They do not establish which vehicles are player-driveable or compatible
with VR. No plane vehicle definitions occur in these installed episode lists.
Rail/cable models are catalogued but require their native tracks and a separate
test setup; ordinary vehicle creation does not validate them.

## Procedure

1. Save a living free-roam player's position/heading with `tools/game-location.ps1
   Capture`. `tools/read-player-transform.ps1 -LocationOnly` can also preserve a
   read-only root orientation snapshot while seated. Snapshots stay under ignored
   `build/profiles/vehicle-validation` and do not write saves.
2. Run `tools/vehicle-test.ps1 Begin` to select the active episode and stored origin.
3. Run `tools/vehicle-test.ps1 Next`. It spawns one model beside the saved anchor,
   restores the on-foot player to that anchor and heading, and reports completed
   native creation. Enter normally to check entry and seating. Boats use a nearby
   queried water point; helicopters have a larger spawn separation.
   Each successful spawn sets the native clock to12:00 for interior visibility.
4. Check camera, hands, one/two-hand steering acquisition/return, visible wheel
   movement, panel placement, radio selection, and entry/exit. Tune the radar and
   radio with the VR menu's grip editing toggle. Phone and drive-by routes should
   also be covered for representative supported vehicle classes.
5. Report `Pass`, `Problem` or `Skip` with a concise `-Notes` description before
   advancing. A passed model's saved panel profile is copied to
   `data/plugins/VRVehicles` for distribution. Problems remain in the local
   resumable session for follow-up. Passing one model does not validate others.
6. Step out before the next model. Cleanup removes only the helper's previous
   empty test vehicle. It preserves other vehicles and refuses occupied cleanup.
   `Finish` removes the last empty test vehicle.

For a self-directed sequence, step out after a successful test and run:

```powershell
.\tools\vehicle-test.ps1 Advance
```

This records the current model as passed, copies its saved panel profile into
the repository, and spawns the next pending model. It reports the new model
name. If a vehicle has a problem, record `Problem -Notes 'description'` and use
`Next` to move on without marking that model as passed. `Spawn -Model admiral`
can revisit a model after a correction. No game restart is needed for these
script commands.

To choose a helicopter testing spot, stand on foot facing clear ground and run:

```powershell
.\tools\vehicle-test.ps1 HeliHere
```

This saves a point 12 metres ahead of the current headset's horizontal facing
direction. Use `-Distance` to change that separation. Subsequent helicopter
`Spawn`/`Advance` commands reuse the saved point and leave the player in place,
instead of using the original beach-side offset. The point survives a game
restart in the local session file. To reposition the current helicopter after
choosing a new point, use `Spawn -Model <current-model>` while it is empty.

The helper accepts explicit model hashes and finite coordinate inputs through
bounded bridge commands. All streaming, creation, positioning and deletion run
on the simulation callback, in unpaused single-player free roam. The helper is
installed only with `EnableCommandBridge=1`; ordinary distributed gameplay has
no vehicle-test callback. A 15-second model streaming timeout returns an error.

Regenerate the catalog after changing installed model definitions:

```powershell
python tools/vehicle-catalog.py --game-dir '<folder containing GTAIV.exe>'
```

## Current coverage

The GTA IV sequence has completed all 127 catalogued models: cars, bikes,
helicopters, boats, the cable car and both subway models. Saved radar/radio
layouts are per model. Rail checks use the native test harness described below;
they do not establish ordinary route boarding or mission coverage.
`Advance` reports completion after the final model. This sequence does not
establish coverage of every vehicle gameplay feature. TLAD/TBoGT inventories
and runtime coverage remain separate and pending.

Validation used GTA IV Complete Edition 1.2.0.59, SteamVR 2.17.10 and Vive Pro
with Wands, using the DXVK/Vulkan bridge. The x86 Release build passes. Cable-car
travel remains seated, with tracked hands and wrist radar; walking inside the
cabin is not supported.

## Rail tests

`Next` now continues into the three rail entries. Each test creates a native
mission train on an existing route and boards the player automatically. The
cable car starts on its cable route; subway models start on the subway route.
The beach anchor remains unchanged. Rail tests temporarily set the maximum wanted
level to zero, filter scripted wanted requests and apply police-ignore/damage
protection, since locked-island scripts can repeatedly force a wanted level.
`LeaveRail` returns to the beach and restores the previous maximum; cleanup also
restores it and removes test police-ignore/invincibility. No island-unlock flags
or saves are changed. `vehicle-test-status` reports protection, wanted level,
health and boarding state for explicit verification.

Cable-car passengers remain seated with freely tracked hands and wrist radar.
Walking inside the cabin is not supported.

For all three rail test vehicles, right trigger moves forward,
left trigger moves backward, and releasing both stops it. Hands remain freely
tracked; there is no wheel steering for a constrained rail route. These test
controls apply only to the helper's owned train in unpaused free roam, and stop
when VR loses focus or a phone/menu opens. Ordinary trains are unaffected.

Before advancing, return safely to the saved beach with:

```powershell
.\tools\vehicle-test.ps1 LeaveRail
.\tools\vehicle-test.ps1 Advance
```

Creation uses the CE 1.2.0.59 native consist definitions. A scoped singleton
consist selects each subway model and is restored immediately after native
creation. Only the helper's own empty train is deleted during cleanup. This is
a test harness, not completed ordinary subway/cable-car passenger support.
