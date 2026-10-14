# Movable vehicle radar and native radio

Tracked under [vehicle interactions #3](https://github.com/bordplate/GTA4VR/issues/3).

## Controls and saved placement

In **VR → Vehicle camera and radar**, enable **Grip to move vehicle panels**.
Grip near the radar or radio with either free hand, then move and rotate the
controller. Releasing saves the panel after a 500 ms debounce. Panel acquisition
has priority over steering-wheel acquisition. Phone and drive-by weapon hands
retain ownership.

With editing disabled, gripping the radio drags its native station carousel.
Hand displacement along the panel's horizontal axis selects the station in the
centre. Selection uses displacement from the initial grip, including vehicle
motion compensation; it does not accumulate frame deltas. Driving also exposes
**Next radio station** and **Previous radio station** actions without default
buttons.

Profiles are stored next to the ASI in `VRVehicles/<model-hash>.ini`, with separate
`[Radar]` and `[Radio]` sections. PositionX/Y/Z is a metre offset from the saved
mount (right, up, backward). `[Mount]` PositionX/Y/Z stores that mount relative
to the vehicle root in the same axes. This reference is shared by rendering and
grip interaction and remains fixed across exits, entry sides and camera changes.
RotationX/Y/Z/W is a quaternion; Width/Height
are metres. Parsing checks finite numeric values, a nonzero quaternion, and
positive dimensions. Vehicle models without a saved section retain the existing
dashboard defaults; the radio starts above the radar. Both panels use the eye's
opaque scene depth and logarithmic depth encoding. Bikes use the same vehicle
panel path.

Legacy profiles without `[Mount]` keep their existing panel offsets. Their next
seated entry records and saves the reference without requiring panel editing.
Use the same entry side as when the layout was positioned for this first
migration entry (the current tested layouts used the driver side: left for cars,
right for the Bus). Entry side no longer affects the panels once the mount is
saved. The old files contain no reference coordinates, so entry side alone
cannot reconstruct an exact mount offline for every model.

Copy tested installed profiles into `data/plugins/VRVehicles` to ship positions
for those vehicle models. Generated placements are not automatically added to
the repository.

## Native radio integration

Static analysis: GTA IV Complete Edition 1.2.0.59, x86 image base `00400000`.

- `0093A570` queues the vehicle radio carousel using `RadioLogo.DAT` parameters.
- `0093A8A0` arranges the native five/seven station logos and highlights.
- `00938790` gates vehicle radio presentation.
- `009391E0` returns the native available station count; `00939210` supplies
  station logo widths.
- `00945210` resolves the native carousel station object; its internal name is
  at `+17FC`, also used by the script station-name getter (`009A3B70`). Tuning
  and readback use this identity. The script index getter (`009CCDA0`) applies
  `0103ACF8`, which aliases several internal station slots to script index 16;
  it cannot identify every native carousel entry uniquely.
- `008DD740` executes `CDrawRadioHudTextDC`, the native `0x30` resolved quad.
  Its eight XY floats start at `+8`, texture at `+28`, tint at `+2C`.

The VR producer scopes presentation overrides around the normal native vehicle
radio call, restoring the UI globals afterward. Native logos, widths and alpha
are retained. Only the resolved drawing command is replayed into an isolated
texture, which is then composited on the vehicle radio plane. Retuning runs on
the simulation callback through native functions. No logos or game assets are
copied into the mod.

Findings are persisted in the Ghidra project under `VRVehicleRadioIssue3`; the
resolved draw function is named `DrawResolvedRadioHudQuad`.

## Validation

### Native vehicle radio capability

The panel, grip interaction and station actions follow the native per-instance
radio capability instead of a model exclusion list. CE `009A3EF0` obtains the
seated vehicle through `0093F1C0` and checks its `+D10` station byte: `FE` means
no radio, while `FF` means switched off. The live Ambulance returns `FE`.
The native HUD visibility override retains this capability check; it only
overrides presentation/fading for supported radios. Ghidra findings are saved
under `VRVehicleRadioCapabilityIssue3`. `GET_PLAYER_RADIO_MODE` is a frontend
preference, not this capability test.

The deterministic pose/drag fixture verifies return-to-start stability, horizontal
station displacement, vehicle-coordinate invariance and structural validation.
All hook signatures match uniquely in the tested CE executable. Release compiles.
Panel movement and saving passed the initial car headset check. Radio capture is
active with 22 native entries in that route. The first build's release mismatch
was traced to script-index aliasing; tuning/readback now use station names and
preserve the requested selection during native retuning. Radio scrubbing and
release now retain the correct displayed/playing station in the tested car.
Bike presentation and flat-screen runtime checks remain pending. TLAD/TBoGT are
untested.
