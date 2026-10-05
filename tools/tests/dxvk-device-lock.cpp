// Exercises the actual pinned DXVK implementation, including the #013 patch.
// Build with -I build/dxvk-013/src and its util/sync/sync_scoped.cpp.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#include "util/sync/sync_scoped.h"

static void Check(bool ok) { if (!ok) std::abort(); }

static void Run(bool releaseStore)
{
    std::fprintf(stderr, "Testing releaseStore=%u\n", unsigned(releaseStore));
    dxvk::sync::ScopedDeviceLock lock(true, releaseStore);
    unsigned counter = 0, complement = ~0u;
    std::atomic<bool> start{false};
    std::vector<std::thread> workers;
    for (unsigned worker = 0; worker < 4; ++worker) {
        workers.emplace_back([&] {
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            for (unsigned i = 0; i < 100000; ++i) {
                auto outer = lock.acquire();
                // A recursive guard must leave ownership with the outer guard.
                { auto inner = lock.acquire(); Check(complement == ~counter); }
                Check(complement == ~counter);
                ++counter;
                complement = ~counter;
            }
        });
    }
    start.store(true, std::memory_order_release);
    for (auto& worker : workers) worker.join();
    std::fputs("Concurrent publication completed\n", stderr);
    Check(counter == 400000 && complement == ~counter);
    // Moving a guard transfers the one outer release obligation.
    { auto guard = lock.acquire(); auto moved = std::move(guard); }
    std::thread afterMove([&] { auto guard = lock.acquire(); ++counter; });
    afterMove.join();
    Check(counter == 400001);
    dxvk::sync::ScopedDeviceLock disabled(false, releaseStore);
    { auto first = disabled.acquire(); auto second = disabled.acquire(); }
}

int main()
{
    Run(false);
    Run(true);
    std::puts("DXVK device-lock recursion, move, concurrent publication and disabled paths passed");
}
