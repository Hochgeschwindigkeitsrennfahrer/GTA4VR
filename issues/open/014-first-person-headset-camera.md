# 014 — First-person headset camera and locomotion

## Goal

Make the on-foot view first-person, with headset rotation and translation driving the viewing camera and visibility following that camera. Physical movement in the room must move the character's body by the comparable game-world distance; controller inputs provide longer-range locomotion. Coordinate measured scale and pose timing with #011. Roadmap stage 3.

## Tasks

- [ ] Identify on-foot camera/body ownership and avoid third-person camera offsets, forced look direction and unwanted head/body clipping.
- [ ] Use headset poses for the first-person eye views and conservative union frusta before world visibility traversal. Test looking behind/around the character and leaning near walls.
- [ ] Apply measured room-scale translation to the character body/collision representation instead of leaving it at the tracking-origin center. Handle walls, recenter, tracking loss and artificial locomotion without counting the same translation twice or allowing unchecked body movement through geometry.
- [ ] Add controller-driven locomotion through #015 and define its directional reference/body-facing behavior. Looking around must remain possible independently of locomotion; test combined physical and controller movement.
- [ ] Validate standing/seated walking, turning, aiming, entering/exiting vehicles and ordinary flat-screen behavior.
- [ ] Preserve separate radar/menu viewport ownership and restore temporary world camera state; do not duplicate UI/script updates. Verify pause/resume and existing desktop UI behavior without choosing a new headset UI layout.

## Done when

A repeatable first-person walking/leaning/head-turn route has coherent body movement and no obvious headset-camera culling holes. Walking a measured physical distance moves the character body a comparable game-world distance, including when combined with controller locomotion. Collision/recenter behavior, movement-direction policy and measured scale/pose limitations are documented.

## Notes / evidence

- First-person camera work is the next priority as of 2026-10-01; #007 pixel attribution is not a blocking dependency. UI placement/interaction decisions remain deferred. Begin with the on-foot first-person anchor, headset-relative orientation and visibility; validate scale and room-scale body/collision movement with #011, then integrate controller locomotion with #015. Keep the issue open until the combined acceptance route passes.
- User clarification on 2026-10-01: controller inputs provide long-term locomotion; physically moving around the room must also move the character by a comparable in-game distance. The body must not remain at the middle of the play space. The controller-direction reference and body-facing policy still need design/validation; do not infer mandatory body yaw from headset rotation. Controller locomotion binding depends on #015.
- Requested on 2026-10-01. Implementation and runtime acceptance remain pending.
