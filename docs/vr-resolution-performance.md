# Prepared screen-dimension mapping

Performance work: [GitHub #2](https://github.com/bordplate/GTA4VR/issues/2), roadmap milestone 5.

## Change

The stereo resource owner prepares desktop-to-eye and eye-to-desktop screen-dimension matchers when allocating targets. Device reset recreates them; scope resources copy the matching pair. Vertex/pixel constant writes and routing transitions reuse those values, avoiding repeated division and CRT float classification in a frequent CPU path.

The matcher retains the existing three complete signatures and reciprocal tolerance of `1e-7`: dimensions plus reciprocals, zero origin plus reciprocals, and reciprocals plus zero padding. Positive tolerance comparisons reject NaNs and infinities. Unrelated shader constants remain untouched. Native rendering, independent eye projection and draw order are unchanged.

## Evidence

Tested on GTA IV Complete Edition 1.2.0.59, x86 Release based on `12ba18e`, DXVK 3.1.1, SteamVR 2.17.10, VIVE Pro and Wands at 90 Hz, RTX 4070 Ti / NVIDIA 610.88. Eye targets were 1974×2192 at scale 0.8; view/detail distance were both 100.

A 10-second native ETW trace had no event loss. The original matcher accounted for 460 exclusive / 605 inclusive samples out of 8,799 matched hottest-thread stacks (5.23% / 6.88%). These sampled shares are not whole-frame savings.

The subsequent matched comparison used one warmup and one measured loop of the same 30-second city flyover, with a fixed stereo rig/FOV. All eight route segments completed with focus maintained, no command-buffer overflows and the explicit frame profiler off.

| Matcher during main-eye constant writes | Stereo submissions/sec | Mean stereo command span, ms |
| --- | ---: | ---: |
| Original | 45.63 | 16.63 |
| Prepared | 47.45 | 15.94 |

This is approximately 4% higher throughput and 0.70 ms less command execution in this series. Traffic, weather and world time remained live, and per-segment differences were uneven; it does not establish an exact isolated gain or all-scene 90 FPS. Earlier comparisons with changed graphics settings were excluded.

Fresh captures showed geometry and lighting in both distinct eye views. Optimized x86 tests verify supported signatures, reciprocal tolerance, unrelated constants, NaN/infinity rejection, reverse/in-place mapping and reconstruction for different desktop sizes. The isolated publication build is x86 Release.

## Remaining work

The expensive native G-buffer and DrawScene paths remain the larger target. Graphics-worker and GPU mesh/model/geometry-atlas replacements are separate unpublished experiments, disabled in ordinary gameplay. They are not part of this optimization. Wider scene, episode and fallback coverage remains open under #2.
