# DXVK experiments for issue 013

These are experimental modifications to public DXVK source, not game binaries.
Upstream DXVK is by Philip Rebohle and contributors and retains its zlib/libpng
license and notices. The patch marks the GTA4VR changes explicitly. Preserve
upstream's `LICENSE` when building or distributing a modified backend.

## Deferred index binding (not accepted)

`013-defer-index-bindings.patch` targets DXVK v3.1.1, commit
`b1a1c99ab52b687cf950d62c88bc2fa316b41663`.

Apply to a clean checkout from the repository root:

```powershell
git -C build/dxvk-013 apply --check ../../patches/dxvk/013-defer-index-bindings.patch
git -C build/dxvk-013 apply ../../patches/dxvk/013-defer-index-bindings.patch
```

Build instructions and exact dependency/tool versions are in
`docs/frame-profiling.md`. This adds a default-off backend setting:

```ini
d3d9.experimentalDeferIndexBindings = False
```

When enabled, a changed non-null `SetIndices` still updates the API-state COM
reference under the original device lock. It marks the existing index-buffer
dirty flag instead of immediately queuing a backend bind. `PrepareDraw` consumes
that flag for ordinary indexed draws; dynamic system-memory index uploads and
UP draws retain their own binding paths. State-block Apply calls the same setter.
Null-binding compatibility and EndScene cleanup remain upstream behavior.

The intended saving is bindings overwritten before a draw. Existing identical
bindings are already skipped upstream; no benefit is assumed from call counts.
The native CPU sample identified SetIndices and index-command construction as
costs, but did not establish how many changed bindings are overwritten.

Before acceptance: compare the same patched DLL with the option off/on/off,
matching held location and headset pose. Preserve configs/backend/PDBs before
deployment, verify actual exit before changing the DLL, and verify the effective
option in a fresh DXVK log. Keep screen capture, compilation and analysis outside
measurement windows. Check both eyes, streaming, vehicle transitions, device
reset and flat-screen behavior. Roll back unless a repeatable gain and unchanged
graphics are established. Keep DLLs, PDBs, configs and traces under ignored
`build/`; do not ship this experiment as an accepted optimization.

Post-restart off/on/off testing on2026-10-05 showed no benefit (mean submission20.7970/21.2833/20.8680ms). The experiment was rolled back; original backend/config restored. Keep default off. See docs/profiling-013-results.md for scene, build and comparison limitations.


## Device-lock release-order experiment (pending)

`013-release-device-lock.patch` is an independent patch for the same pinned
v3.1.1 commit; reverse the index patch before applying it. It adds the default-off
`d3d9.experimentalReleaseDeviceLock = False` setting. Only the D3D9 device passes
this option to its existing scoped recursive lock; other callers retain the
original default. When enabled, the outer guard publishes unlocked ownership
using `memory_order_release` instead of the original sequentially consistent
store. Acquire compare-exchange, contested waiting, recursion, COM references
and device protection remain intact. This is separate from the Vulkan queue lock.

Rationale from source: successful acquisition uses acquire ordering, and the
same source tree's ordinary spinlock already publishes unlock with release
ordering. This does not prove a frame-time gain or measured lock contention.
Test `tools/tests/dxvk-device-lock.cpp` against the actual patched header and
`src/util/sync/sync_scoped.cpp`, rather than a copied implementation. In an x86
VS developer shell:

```powershell
cl /nologo /std:c++17 /EHsc /O2 /MT /DNOMINMAX /Ibuild\dxvk-013\src tools\tests\dxvk-device-lock.cpp build\dxvk-013\src\util\sync\sync_scoped.cpp /Febuild\dxvk-device-lock-test.exe /Fobuild\
build\dxvk-device-lock-test.exe
```

The test covers nested guard ownership, moved guards, disabled protection and
four threads publishing/checking protected state. A passing stress test is not a
formal proof of all uses. Build Debug and Release, verify generated x86 unlock
instructions, and compare identical-DLL off/on/off before promotion. Preserve the
original backend and config; revert if there is no repeatable gain.

### Guard move correction (separate prerequisite)

`013-device-guard-move.patch` corrects the pinned source's move constructor from
`std::exchange(m_lock, nullptr)` to `std::exchange(other.m_lock, nullptr)`.
The actual-header test reproduced an access violation after concurrent testing
when explicitly moving a guard, before this correction. With the correction,
both ordering modes pass the nested/concurrent/move/disabled checks. Do not infer
that any observed GTA IV crash used this move constructor. Runtime comparisons
include this correction identically in both controls and the enabled case; it
must not be counted as a release-order performance saving.

The x86 MSVC /O2 assembly probe confirms enabled unlock uses an ordinary `mov`
store while the default path uses memory `xchg`. Acquire still uses the original
compare-exchange. The guard remains a mutex with release/acquire publication;
there is no removal of locking or resource references. Binary code-generation
and stress evidence alone do not establish game performance or all-path safety.

Off/on/off runtime test completed2026-10-05: eye-command sums15.9066/15.4444/15.4237ms and submission22.5038/22.3369/21.5588ms. Enabled matches repeat control; no repeatable gain. Release-order option not promoted, original backend/config restored. Detailed evidence and limitations in docs/profiling-013-results.md.
