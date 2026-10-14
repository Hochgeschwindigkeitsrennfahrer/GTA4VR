# Vehicle panel placements

Edited radar and radio placements are saved here as `<vehicle-model-hash>.ini`.
Each file can contain independent `[Radar]` and `[Radio]` sections. Positions are
in metres relative to the saved mount, with X right, Y up and Z backward.
`[Mount]` records that reference relative to the vehicle root in the same axes,
so entry side and camera calibration no longer move either panel. Old files
without this section learn the reference on their next seated entry; use the
original entry side for that one migration entry. Existing panel values remain
unchanged.
RotationX/Y/Z/W is a normalized quaternion. Width and Height are in metres.

Vehicle models without a profile use the existing dashboard radar settings.
The radio starts above the radar. Profiles can be copied from a tested game
installation into this directory to distribute model-specific defaults.
