#include <cstdio>
#include <cstdlib>
#include "../../source/includes/vrtexturerouting.hpp"
void Check(bool ok) {if(!ok) std::abort();}
int main()
{
    alignas(16) char storage[128]{};
    void* original=storage; void* left=storage+16; void* right=storage+32;
    void* ordinary=storage+48; void* collision=storage+64;
    unsigned scans=0;
    auto resolve=[&](void* input) {
        ++scans;
        return input==original || input==left || input==right
            ? std::array<void*,3>{original,left,right}
            : std::array<void*,3>{input,input,input};
    };
    VRTextureRouting::Cache<void*,4> cache;
    Check(cache.Map(original,0,resolve)==left);
    Check(cache.Map(original,1,resolve)==right && scans==1);
    Check(cache.Map(original,-1,resolve)==original && scans==1);
    Check(cache.Map(left,1,resolve)==right);
    Check(cache.Map(right,0,resolve)==left);
    Check(cache.Map(ordinary,0,resolve)==ordinary);
    const auto before=scans;
    Check(cache.Map(ordinary,1,resolve)==ordinary && scans==before);
    Check(cache.Map(nullptr,0,resolve)==nullptr && scans==before);
    Check(cache.Map(original,2,resolve)==original && scans==before);
    Check(cache.Map(collision,0,resolve)==collision);
    Check(cache.Map(original,1,resolve)==right); // Same hash slot, full key checked.
    auto changed=[&](void* input) {++scans;return std::array<void*,3>{input,ordinary,collision};};
    VRTextureRouting::Cache<void*,4> recreated;
    Check(recreated.Map(original,0,changed)==ordinary);
    Check(recreated.Map(original,1,changed)==collision); // New resource owner, no stale route.
    std::puts("Texture routing identity, eye selection, collisions and resource replacement passed.");
}
