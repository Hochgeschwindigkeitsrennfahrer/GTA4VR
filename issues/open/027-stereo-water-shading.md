# 027 — Fix stereo water shimmer and reflections

## Goal

Correct the reported water shimmer/reflection mismatch between the eyes. Depends on #006; this is the explicit water repair within #011 auxiliary-view validation. Roadmap stage 3.

## Tasks

- [ ] Capture water in both eyes while standing, rotating/leaning and moving; record location, weather/time, settings and runtime.
- [ ] Inspect reflection/refraction target ownership, water shader camera/projection constants, temporal state and draw scheduling. Test the shared game-camera auxiliary-view hypothesis rather than assuming it is the cause.
- [ ] Implement required per-eye inputs or auxiliary views without duplicating simulation or creating inconsistent animation time.
- [ ] Verify geometric parallax, reflected/refracted features and temporal stability across representative water views; compare flat-screen output and measure added rendering cost with #013.

## Done when

Water has coherent physically expected binocular differences and stable shimmer/reflections during the recorded route, with no unexplained eye mismatch. Per-eye evidence and remaining water cases are documented.

## Notes / evidence

- User report on 2026-10-01: water does not shimmer/reflect the same in both eyes. Eye images should not be forced identical; diagnose incorrect correspondence versus legitimate viewpoint differences. #006 currently shares game-camera reflection/weather views.
- Requested on 2026-10-01. Implementation and runtime acceptance remain pending.
