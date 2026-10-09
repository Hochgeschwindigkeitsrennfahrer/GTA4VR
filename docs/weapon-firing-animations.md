# Weapon firing animations

Under #024, successful tracked shots on the Glock (type7), combat pistol
(type9), SMG (type13), M4 (type15) and combat sniper (type16) request the installed Animated Weapons `weapon_fire`
clip on the actual held weapon object. Dictionaries are `gun@handgun`,
`gun@deagle`, `gun@mp5k`, `gun@ak47` and `gun@rifle`, matching this install's weapon data. The mod's
reference implementation uses hash0xDF8E89EB for this same clip.

This adapter uses the game's `PLAY_OBJECT_ANIM` native with nonlooping playback.
It does not start a character fire/reload task or change native firing cadence,
damage, ammunition or VR arm recoil. Only successful shots request animation;
dry clicks do not. Missing clips leave shooting functional.

Physical reload keeps control of magazine visibility and the slide/handle while
manually racking. Between racks the native animation controls the slide/bolt
palette, rather than resetting it to the rest pose every frame. Other weapon
bones remain native. Loose magazine palettes remain isolated from the gun.

`reload-status` includes `fireAnimations` (native accepted requests) and
`fireAnimationMisses` (unavailable object/dictionary or rejected playback).
These counters do not prove visible clip playback. Check each weapon while
shooting, then rack/reload to verify pose ownership returns correctly.
`fireAnimationError` identifies the last failure:1 means no validated object
handle,2 means the dictionary was not loaded (requested for subsequent shots),
3 means native playback was rejected. Zero means no failure or a successful
request. Initial live shots failed because the handle lookup rejected inline
object-pool storage. The user accepted visible playback for Glock, combat pistol
and M4 on 2026-10-07, including manual-rack coexistence. The SMG interaction
is also user-accepted. Combat sniper playback and reload remain pending.

`weapon-animation-test` queues one clip request on the simulation callback,
under the same tracked/focused/free-hand reload gates. It does not fire or
consume ammunition. Check the counters afterward; acceptance of the command
alone does not establish successful playback.

Animation/model assets remain an external dependency. None are redistributed
in this repository; existing dependency attribution and installation apply.
