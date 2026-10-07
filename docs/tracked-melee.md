# Tracked melee (#017)

## First pass: unarmed punches

`[VR] ExperimentalHandCombat=1` enables tracked punches with hand IK and
`ExperimentalHandPhysics=1`. The distributed default is off. Both hands can punch
when unarmed. With the standard pistol equipped in the right hand, the free left
hand can punch using the same fist/speed/contact gates. The weapon hand, active
pistol support grip and acquired physical objects suppress punches on that hand.
Other equipped weapon classes remain disabled until their ownership is covered.

- Each fist requires its **Close free hand** grip action and **Curl index finger**
  trigger action (70% squeeze to enter, 40% to exit). The thumb is unrestricted.
- `HandPunchSpeed=1.6` is the initial speed threshold in metres/second, measured
  from OpenXR grip positions rather than player movement. Running into someone
  with a stationary fist does not qualify.
- A 7.5cm native swept sphere covers the palm between published poses. The
  closest world contact consumes the strike; walls and props can block it.
  Player collision roots and owned limb proxies are excluded before hit writes.
- One contact per strike, with a 300ms minimum interval. Opening the fist or
  holding it below 0.7m/s for 60ms rearms it. Tracking gaps/invalid data disarm it;
  pause, phone, tracking loss and vehicle transitions clear strike history.
- `HandPunchDamage=25` is initial native unarmed damage, pending gameplay tuning.
  Both settings apply after restart. Supported ranges are 0.8..5m/s and 1..100.

No attack-button injection or attacker animation task is used. Native game
damage/events own proofs and damage processing. Victim damage events and the
native global melee-action event carry player attribution;
tracked arms retain IK. Other weapon volumes, blocking and haptics remain
follow-up work in #033. GTA IV, TLAD and TBoGT acceptance is tracked separately.

## Native endpoint evidence

CE1.2.0.59 / x86 / Ghidra image base00400000:

- `CD4590`, native melee action-result damage application, calls `B60C80` at
  `CD4725`. Caller cleans 0x24 bytes: nine cdecl arguments, float result in ST0.
  Its +54 damage and +5C bit80 make native application occur once.
- Signature: `float B60C80(attacker, victim, weaponType, damage, origin*, hit*,
  optionalWeapon*, extraFloat, reactionOwnedByMeleeTask)`. The initial punch pass supplied
  weaponType0, tracked sweep origin/hit, optionalWeapon=null, extraFloat0,
  reactionOwnedByMeleeTask=false. The initial true value suppressed nonfatal
  reaction/event delivery because GTA IV expected a melee animation task that
  VR does not create. Runtime testing exposed this error (damage/voice only).
- `B60C80` calls `CA2A70`/`CA2B60`/`CA3820` for the damage event/calculation,
  then native acceptance/event/reaction handling. It explicitly supports a null
  optional weapon. Construction/type setup alone (`B5C120`, `B64030`) leaves
  +50/+54/+58 zero. Later tracing found the melee caller then sets +50/+54;
  the current tracked path supplies that populated temporary context, as
  described in the impact correction below. Full reaction parity remains pending.
- With last argumentfalse, the directional native victim-response path runs
  through `CA3F70` and queues the damage event at `B6199D`. With true and a null
  weapon, a nonfatal hit can jump directly from `B61615` to `B619E4`, skipping
  the queue. The attacker pointer in a calculated damage response alone does
  not prove that AI received that attribution.
- Native melee also constructs `CEventMeleeAction` (`B45440`,0x24 bytes) with
  actor+18, opponent+1C and attack flag+20. `CCF588..CCF5BF` constructs it,
  obtains the global event group (`B50A30`), calls `B506A0(event,false,true)`
  and destroys the stack event (`B45E30`). VR uses the same lifecycle once per
  accepted tracked hit. The ctor/dtor own native entity references; group Add
  clones the event. `B4A9B0` identifies the player from actor+18, and `B483A0`
  filters affected pedestrians, including the selected opponent.
- `A535E0` is the existing native volume query. Final argument0 chooses the
  swept path through `738A80`; stationary grabs use final argument1. Hit records
  are 0x60 bytes: instance+0, point+10, part index+52. Native damage uses the part
  and origin for hit location/direction (`B5FD50`).

Pointers are resolved by unique runtime signatures, not fixed addresses.
CD4590 and B60C80 findings were bookmarked in Ghidra under Issue017, read back
and saved.

## Inspection and acceptance

`tools/game-command.ps1 combat-status` returns readiness, fist states, measured
speeds, sweep/contact counts, per-hand accepted damage counts, last native damage
and target handle. It has a separate response to keep `hand-status` within its
4096-byte limit. Counts show coverage, not visible combat acceptance.

The deterministic `tools/tests/vrpunch.cpp` checks movement isolation, open
hands, duplicate samples, one-hit/rearm/cooldown and tracking discontinuities.
Unarmed punch reactions and pistol off-hand punches are user-accepted. Extended
pause/tracking transitions and ordinary flat-screen melee still require evidence.

### Reaction correction

User confirms the first build damages NPCs but produces only an "ouch": no
flinch, flight or aggression. Combat attribution/reaction acceptance therefore
failed. The revised path enables native directional damage reactions and
broadcasts the native melee action. Newly closed unarmed fists also suppress
new grab acquisition, avoiding auto-grabbing a knocked-down target mid-strike;
existing acquired holds continue normally. The user confirms the corrected
unarmed reactions work. Pistol off-hand punches were subsequently accepted too.

## Tracked knife

The user accepts pistol off-hand punches as reasonably working. The first knife
profile uses native equipped weapon type3 (`w_knife`) and the same opt-in combat
setting. Move the equipped knife physically to swing or stab; there is no trigger
requirement or native attacker swing animation. Slow contact does not qualify.

- Five 3.5cm sphere samples sweep the cutting section of the rendered weapon,
  with the earliest contact consuming the entire strike. Owner, held weapon and
  owned hand proxies are excluded; nearby walls/props remain blockers.
- `HandKnifeSpeed=1.2` metres/second measures the fastest blade sample in
  tracking space, including wrist rotation. Player movement is removed through
  the published tracking-to-world transform. Stale or invalid poses
  disarm it; weapon changes reset history. Hold the knife still briefly to arm.
- One accepted contact per strike, at least 180ms apart. Slow below 0.7m/s for
  20ms, or withdraw the fastest impacting sample by 6cm opposite its strike
  direction, to rearm. This permits repeated slashes/stabs without deliberately
  pausing. Continued penetration cannot rearm; stationary overlap never meets
  the speed gate. Knife and punch gates are
  independent. Pause, tracking loss and vehicle transitions reset both.
- `HandKnifeDamage=25` is configurable native knife damage. Native `melee.dat`
  lists 10/10/25 for its knife combo; this first pass uses its strong attack value
  without claiming full combo/block/counter parity. Range1..100; restart to apply.
- `combat-status` also reports `knifeReady`, blade speed, thresholds and
  knife sweep/contact/hit counts. `knifeMissSteps` counts eligible blade intervals
  that hit nothing; `knifeRearmBlocked` and `knifeCooldownBlocked` count fresh
  fast intervals suppressed by those gates. `knifeTrackingResets` counts sample
  discontinuities. `knifeNativeRejected` counts contacts denied by native damage
  handling; `knifeNonLivingContacts` counts other unsuccessful contacts (including
  props/walls/dead targets). These are interval counts, not attempted-stroke counts.
  Readiness requires the current equipped knife
  and a fresh, plausible published blade pose, not just resolved native calls.

CE1.2.0.59 read-only evidence: AF65D0 cdecl WeaponInfo lookup (entry stride110),
type3 +C damage category1 and +10 sharp group3. The drawable has `gun_grip` but
no `gun_muzzle`. Its observed bounds are min(.037956,-.001041,-.078821),
max(.092381,.025205,.247477). `vrmelee.hpp` profiles the positive-Z cutting
section z=.06..247477, centered x=.065169/y=.012082. These are a bounded geometry
approximation for this asset; exact edge/handle exclusion still needs gameplay
evidence. The real equipped entity matrix drives placement. Passing type3 to
B60C80 preserves native knife classification with the accepted reaction/event
path; no manual health write. AF65D0 finding was bookmarked, read back and saved
in Ghidra. Raw process observations remain ignored in build/.

User accepted the revised knife detection as working well enough on 2026-10-07.
Deterministic blade tests cover placement, translation and rotational swings,
stationary blade with moving body, cooldown/rearm, duplicate timestamps, gaps,
invalid values and uncapped fast motion. The knife is accepted for this pass. GTA IV is the first target; episodes
remain unverified.

## Baseball bat and pool cue

Stock CE WeaponInfo.xml lists type1 `w_bat` and type2 `w_cue` as the remaining
equipped melee weapons. Both use MELEE_CLUB/MELEE_WOOD. There is no equipped
crowbar in this install; legacy entries in melee.dat are not proof of available
weapons. Physical swings/stabs use the same withdrawal/cooldown gate as the knife.

- Loaded drawable min/max bounds author the club contact line along model Z,
  excluding the first 10cm around the grip. Invalid/oversized/unexpected shapes
  are rejected. Bat radius is 5.5cm, cue radius 3.5cm. Sample spacing stays below
  the sphere diameter, with at most 24 samples; the knife still queries five.
- The actual equipped weapon matrix drives the line. Rotational tip speed counts;
  walking with a stationary weapon does not. One earliest world contact consumes
  the whole stroke. Nearby walls/props remain blockers.
- No melee weapon or fist has an upper speed/displacement cap. The original
  12m/s/.75m tracking heuristic rejected fast bat swings, and a reach-based
  extension still imposed an unnecessary cap. Invalid/non-finite or stale poses,
  tracking loss, ownership/equipment changes and existing contact/rearm/cooldown
  guards remain. Minimum attack speeds prevent slow contact from causing damage.
- `HandClubSpeed=1.2` (0.5..5m/s) and `HandClubDamage=30` (1..100) apply after
  restart. Native action_table.csv Club_Hook_R_Res lists damage30; that result
  context plus the actual bat/cue type selects native club reactions and impacts.
  There is no forced vanilla attacker swing animation.
- `combat-status.meleeWeaponType` is 1/2/3 for bat/cue/knife, or zero when unusable;
  `meleeSamples`/`meleeRadius` describe the active shape. Historical `knife*`
  motion/contact counters now cover the active equipped melee weapon; distinguish
  it with meleeWeaponType. clubThreshold/clubDamage report club tuning.
- Bat/cue two-handed support attachment, weapon collision response against props,
  haptics and episode coverage remain follow-up work. This pass supplies tracked
  contact-based NPC combat; it does not claim those features.

The developer bridge accepts only `melee-test bat`, `melee-test cue` or
`melee-test knife`. It loads the native weapon model, gives/equips it on the
simulation callback and verifies selection. `melee-test-status` reports pending,
valid, weaponType and error. Requires living, unpaused on-foot free roam outside
missions/cutscenes. Model loading times out after 30 seconds. This replaces the
native melee inventory slot for testing; it does not issue a save command.
Use only during development. Gameplay shape/reaction acceptance remains pending.

User acceptance: the baseball bat's ordinary and fast swings work after removing
the upper motion caps. Pool-cue swings/jabs are also accepted for NPC combat.

### Native impact and blood correction

The first knife test lacked vanilla blood and both punch/knife reactions felt
weak. B60C80 alone is not the native melee contact sequence. CD5A30 constructs a
native temporary CWeapon (B5C120/B64030/D8C680), sets its melee flag +50 and attack
result hash +54. B60C80 uses that context to select native melee victim responses,
including blocking and fatal/NaturalMotion handling. The tracked path now supplies
Jab_Res, cross_R_Res or Blade_Med_S_A_Res, verified against the installed native
action table; it still does not start an attacker animation.

B5DF50 separately invokes B5E820 after damage. This thiscall takes the temporary
weapon, attacker, origin matrix, native 0x60 hit, damage and near-flesh flag
(RET14). For flesh it calls D5F760: native bloodFx selection, spray via D60930,
world stain projection via D5FA40, and clothing stain via B5B900. The tracked
path now invokes that native impact handler once after accepted damage.
`combat-status.nativeImpacts` counts calls, not visible blood acceptance.

The B636C0 force forwarding path is gated by the native network check; it is not
evidence that single-player applies a universal extra shove. This correction
restores native reaction context rather than adding an unverified impulse on top.
Visible impact parity still needs a gameplay check. CA3F70 has a reaction timer
(+DF0, 500ms written for player targets); this does not establish NPC damage
invulnerability. User reports improved effects but unreliable follow-up hits.
The initial slow-only rearm/350ms gate has therefore been replaced with the
withdrawal/short-pause rearm above, plus a 180ms cooldown and slightly wider blade
samples. Native damage protections/reactions are unchanged. Gameplay acceptance
of this detection revision remains pending.

B5E820 and CD5A30 findings were bookmarked, read back and saved in Ghidra.

### Vehicle and object strikes

The earlier tracked hit delivery only accepted entries in the Ped pool. Sweeps
could hit cars/props, but those contacts never reached their damage handlers.
Non-Ped contacts now use native B5DF50, the same dispatcher called by CD5A30:
`thiscall(CWeapon*, attacker, originMatrix, hits, count, damage, vehicleExtra,
modifiers)`, seven stack arguments/RET1C, float return in ST0. The native melee
caller supplies one 0x60 hit and vehicleExtra=0; VR supplies unity damage
multipliers and the existing temporary melee weapon context.

The dispatcher selects B5E500 for vehicles (A36340 damage/deformation and
B234D0 physics impulse), B5F060 for objects (damage, mass-dependent impulse and
native protection/breakable checks), and B5E820 for impact effects. It also
performs its common player/impact bookkeeping. NPC delivery retains the accepted
damage/reaction/event path. No synthetic extra force or forced object damage is
added. Entity resolution mirrors 9FD420's instance flag check; unregistered
physics hits are rejected as in the native dispatcher.

`combat-status` adds `worldMeleeCalls`, `vehicleMeleeCalls`, `objectMeleeCalls`
and `lastWorldMeleeDamage`. These count dispatcher calls, including protected
contacts with zero returned damage; they do not prove visible damage. The
dispatcher signature matched once in the installed CE executable. Findings
were previewed, bookmarked, read back and saved in Ghidra at B5DF50 under
Issue017WorldMelee. Vehicle/prop gameplay was accepted by the user on 2026-10-07.

## Accepted milestone — 2026-10-07

The user accepts the current punches, knife, bat, cue and native car/prop hits and requested #017 closure. Earlier pending-test statements above describe historical iterations. Haptics, physical weapon blocking/support grips and expanded lifecycle, episode and VR-disabled regression remain explicitly unverified/unimplemented in #033. No new build or runtime changes were made for closure.
