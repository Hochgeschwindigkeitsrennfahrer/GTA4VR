#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <cmath>

namespace VRPhoneInput
{
    template<class Input> std::array<bool,6> Buttons(const Input& input)
    {
        std::array<bool,6> buttons{}; // up/down/left/right/accept/back
        if(!input.phoneActive) return buttons;
        if(input.phoneClick)
        {
            const float x=input.phoneDirection[0],y=input.phoneDirection[1];
            if(std::hypot(x,y)<.35f) buttons[4]=true;
            else if(std::abs(x)>std::abs(y)) buttons[x<0 ? 2:3]=true;
            else buttons[y>0 ? 0:1]=true;
        }
        buttons[0]|=input.phoneUp;buttons[1]|=input.phoneDown;
        buttons[2]|=input.phoneLeft;buttons[3]|=input.phoneRight;
        buttons[4]|=input.phoneAccept;buttons[5]=input.phoneBack;
        return buttons;
    }

    struct Edges
    {
        std::array<bool,6> previous{};
        std::array<uint64_t,6> presses{};
        void Sample(const std::array<bool,6>& buttons)
        {
            for(unsigned i=0;i<6;++i) if(buttons[i] && !previous[i]) ++presses[i];
            previous=buttons;
        }
    };

    struct Pulse
    {
        uint64_t consumed=0,until=0,releaseUntil=0;
        unsigned pending=0;
        bool down=false;
        bool Update(uint64_t presses,bool held,bool enabled,uint64_t now)
        {
            if(!enabled)
            {consumed=std::max(consumed,presses);pending=0;down=false;until=releaseUntil=0;return false;}
            if(presses<consumed)
            {consumed=presses;pending=0;down=false;until=releaseUntil=0;return false;}
            pending=static_cast<unsigned>(std::min<uint64_t>(4,pending+presses-consumed));
            consumed=presses;
            if(down && now>=until && !held) {down=false;releaseUntil=now+50;}
            if(!down && pending && now>=releaseUntil)
            {--pending;down=true;until=now+120;}
            return down;
        }
    };
}
