# AK-47 physical reload adapter

Issue024, GTA IV Complete Edition native type14, external Animated Weapons
w_ak47 asset (separate dependency). Keeps native automatic cadence, muzzle
firing/damage, aiming attribution and gun@ak47 weapon_fire animation.

Right grip ejects the magazine, losing its remaining ammunition. Left grip
over the shoulder supplies a replacement. Guide it into the magazine well,
then pull/release the right-side bolt to chamber if empty. Tactical reloads
retain the chambered round. Clip/chamber/reserve state persists separately
from the M4 across weapon selection. One-handed recoil moves the arm/weapon;
using the support grip reduces it.

Edit plugins/VRWeapons/ak47.ini and run reload-weapon-config while equipped.
Magazine hand pose, insertion tip/axis/travel, charging grip and recoil are
independently tunable, with finite/type/structural validation only.

Reference rig: Mag b6 and Gun_Breach b9 beneath AK_stuff b5. Parent transform
must be included in weapon-relative insertion coordinates. Initial magazine
pivot(-0.0711513,-0.0113021,0.0185908)m; live readback pending. Both M4 and AK
bone names occur in these external assets; select the AK bones for type14.

rifle-test ak47 selects type14 without teleport/save edits, preserving owned
ammo (60rounds if absent). reload-status reports asset/config/ammo state.
Live headset firing/reload/support acceptance remains pending.

ReleaseB822E8BE passed optimized ammo tests and deployed on2026-10-09. Live PID28460 module base00E50000 confirms type14/W_AK47 and the reference bone translations. Adapter active/assetReady/configValid; six shot animations with zero misses, one ejection and one insertion observed. Fresh apartment image shows weapon/arms and intact world rendering. Headset ergonomics/bolt/recoil acceptance pending.

User confirmed all requested AK-47 firing/reload/charging/support checks on2026-10-09. The accepted build remains running.

SupportHandRotationX/Y/Z in [Reload] rotates the support palm around its grip seat, in weapon-local degrees. Zero preserves the authored orientation. Run reload-weapon-config after editing; the supported runtime already reads these keys.

SupportHandOffsetX/Y/Z in [Reload] adjusts the support grip position in weapon-local meters. Zero preserves its authored position. Apply with reload-weapon-config.
