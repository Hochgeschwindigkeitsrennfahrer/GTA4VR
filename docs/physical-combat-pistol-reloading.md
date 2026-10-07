# Combat pistol physical reload adapter

Work under #024 extends the shared firearm adapter to native type9, DEAGLE,
model `w_eagle`. The installed weapon data specifies a nine-round clip,
40 base damage and333ms between shots. These are native data values, not
new VR damage or cadence overrides.

The right trigger fires once per squeeze from the tracked muzzle. Right grip
ejects; left grip over the shoulder supplies a magazine. Guided insertion and
slide racking use the same mechanics as the accepted standard pistol, with
independent settings and saved chamber/magazine state across weapon switches.
The external Animated Weapons model remains a separate dependency.

The combat pistol's `Gun_Cock1` controls the hammer, as observed in the first
headset test. Racking now targets `Gun_Breach` (bone8), whose rest pivot is
-0.0839226/-0.00060389/0.0591172m. This changes both acquisition/hand attachment
and animated rearward travel. The Glock/M4 rack bones are unchanged; visible
combat-pistol slide movement still needs confirmation in the corrected build.

Edit `plugins/VRWeapons/deagle.ini` and run `reload-weapon-config` with the
combat pistol equipped. Check `reload-status` for weaponType9, configValid,
configRevision and assetReady. Initial placement is provisional until live
bone inspection and headset tuning; the Glock profile must not be changed to
tune this weapon. Recoil initially uses8degrees per successful shot,0.15
support scaling,0.18s recovery and35degrees maximum. It moves the weapon and
arm, without moving the headset camera or changing damage.

For development, `pistol-test deagle` selects it through the native equipment
path. Existing owned ammo is retained; an absent weapon receives18rounds.
This command changes inventory for testing, without writing saves or teleporting.
