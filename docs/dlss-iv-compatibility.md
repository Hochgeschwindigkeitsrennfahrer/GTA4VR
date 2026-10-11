# DLSS-IV compatibility

Tracked in [issue #18](https://github.com/bordplate/GTA4VR/issues/18).

## Current functional fallback

With DLSS-IV installed, edit its `plugins/DLSS-IV.cfg`:

```ini
threaded=0
quality=0
```

Restart GTA IV. This disables DLSS-IV's separate graphics-device thread
and selects full-resolution DLAA, giving up lower-resolution upscaling.
Leave GTA4VR's OpenXR and Vulkan settings enabled. It does not require
replacing the VR ASI or uninstalling DLSS-IV.

With threaded0 alone and quality1, the user confirmed initialization but
reported missing opaque geometry. Adding quality0 restored ordinary world
rendering in both eyes; the user confirmed this in gameplay.

DLSS-IV's config describes `threaded=1` as its largest performance gain, so
this workaround gives up that feature. It is not a measured performance
improvement for GTA4VR. Compatibility with DLSS-IV's per-eye upscaling and
temporal history has not been established merely by restoring VR startup.

## Evidence (2026-10-11)

- GTA IV Complete Edition1.2.0.59, SteamVR2.17.10, Vive Pro and Wands90Hz,
  NVIDIA RTX4070 Ti, DXVK3.1.1.
- GTA4VR Release ASI SHA256:
  `53773E66C1D642E7D1687177F13AF3D577C745B9682F1ED09BBC7C00B1522C5B`.
- DLSS-IV build `f5ecb8e7`, Quality preset, responsiveness4, HDR off,
  unpin1, cpu_place1. The installed ASI and VR configuration were intact.
- With threaded1, PID28416 reported OpenXR enabled but failed, no active
  session, no eye images, and no stereo pairs. The bridge responded once
  this run was fully initialized; earlier timeouts did not prove that the
  ASI initialization was absent.
- Changing threaded to0 restored successful OpenXR session, loading panel,
  and eye swapchain creation. PID6680 reported FOCUSED continuous stereo
  at1974x2192 with916 submitted layers. This isolates the startup failure
  to the threaded-device path, but not its exact failing interop operation.
- Both configs were backed up under ignored `build/dlss-compat-backup-*`.
  Explicit startup logs were saved in ignored
  `build/dlss-compat-threaded-off`; no third-party binaries are redistributed
  or modified. DiagnosticLogging is restored to0 for the next launch.

The threaded-off Quality-mode log reports its FP16 target table is full24,
with a987x1096 scene target not paired. The user's screenshot shows sky and
emissive lights but much of the opaque world missing. Testing `quality=0`
(full-resolution DLAA) with threaded0 restored geometry in both explicitly
captured eye images and the user's gameplay check. PID36840 reports focused
continuous stereo1974x2192. diagnostics-status confirms all four passive
diagnostic controls are off.

These changes disable the two major features relevant to performance:
separate device-thread submission and lower-resolution rendering/upscaling.
DLAA remains configured, but whether the headset images receive it is
unverified. DLSS-IV's target-table warning persists; successful ordinary
stereo rendering does not establish per-eye temporal processing. Its other
settings (unpin, cpu_place, responsiveness) remain enabled, with no measured
benefit claimed. This is a functional fallback, not full VR integration.

Issue18 remains open for the exact threaded interop failure and a path to
retain real upscaling/threading benefits. No third-party code was changed,
and no GTA4VR source patch or new build was needed for these tests.
