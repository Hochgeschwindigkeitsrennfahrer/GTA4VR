# Bolt-action sniper physical adapter

Issue024, GTA IV Complete Edition native type17/w_rifle, distinct from the
combat sniper type16/PSG-1. External Animated Weapons remains a dependency;
no weapon assets are included in this repository.

Right trigger fires one native tracked-muzzle sniper shot per squeeze. Each
shot empties the chamber. Hold left grip on the bolt handle, pull backward
then push forward before releasing to feed the next round. Releasing after
the backstroke alone does not chamber. This first pass models longitudinal
bolt travel; it does not require a separate handle-lift/locking rotation.

Right grip ejects the magazine (remaining magazine rounds are lost). Left
shoulder grip supplies a replacement magazine. Guide it into the well,
then cycle the bolt if empty. Ammo/chamber state persists separately from
PSG-1 and other weapons across selection. Supported recoil, native aiming
attribution, scoped precision and delayed-bullet collision filtering reuse
the accepted sniper path.

plugins/VRWeapons/sniper.ini has independent Reload and Scope sections.
SupportHandRotation/OffsetX/Y/Z, magazine insertion and bolt grip/travel are
runtime tunable with reload-weapon-config. Scope uses the same accepted
optical rendering, depth and final weapon-pose routing as the combat sniper.
The user-configured eyepiece placement is accepted and stored in the distributed profile.

External reference rig: gun_stuff b1 parent(+.012,0,.004)m; Mag b3 gives
weapon-relative pivot(-.136978,0,-.01641)m. Gun_Cock1 b9 and Breach b10 are
moved together for manual bolt travel. Live type17/W_RIFLE readback confirms these bones and translations.

sniper-test bolt selects type17, preserving owned ammunition (20rounds if
absent), without teleport/save changes. Release and manual interaction acceptance passed.

Optimized ammo tests cover detachable-magazine losses, mandatory cycling and saved empty chamber. ReleaseD76844AF built/deployed on2026-10-09. Scope accepts finite placement values without guessed range limits, keeping positive optical dimensions and perspective FOV structural checks.

Live PID2540/base00E50000 confirms type17/W_RIFLE, Mag3/Gun_Cock1 9/Breach10 and parent translations. Active adapter/config/asset state, two shots/no animation misses and two bolt cycles recorded; scope renders advancing without failures. The user accepted the configured reload, scope and weapon interactions.

User confirms the initial sniper interactions after tuning. Sight alignment correction separates the physical lens/eye box from the optical camera: render the magnified scene along the native muzzle ray, preserving eyepiece placement and bullet behavior. This removes the constant sight-height/side offset for both type16 and17. Deterministic projection checks pass across weapon rotations and1/10/100m ranges. ReleaseF104989A built successfully and runs on GTA IV CE1.2.0.59 with SteamVR2.17.10/DXVK-Vulkan; the user confirms both snipers now work correctly on2026-10-09. Longitudinal bolt travel remains the supported interaction; separate handle-lift/lock rotation and broader episode coverage remain unverified.
