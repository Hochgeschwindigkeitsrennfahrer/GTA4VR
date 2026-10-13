#pragma once
#include <array>
#include <cmath>
#include <optional>

namespace VRSettingsModel
{
    enum class Number : unsigned
    {
        PlayerHeight, VehicleEyeHeight, EyeRenderScale, HudWidth, HudDistance,
        WristSize, WristBack, WristLift, DashboardSize, DashboardForward,
        DashboardDown, DashboardRight, DashboardTilt, BodyForward, Count
    };
    enum class Flag : unsigned { DisableOpenXR, PhysicalCrouch, ShowBody, AmmoCounters, Count };
    struct NumberSpec
    {
        const char* key;
        const wchar_t* label;
        const wchar_t* unit;
        float fallback,minimum,maximum,step; // Menu range only, not INI validation.
        bool positive;
    };
    inline constexpr std::array<NumberSpec,unsigned(Number::Count)> numbers{{
        {"PlayerHeight",L"Standing eye height",L"m",1.75f,.5f,2.5f,.01f,true},
        {"VehicleEyeHeightOffset",L"Seated eye offset",L"m",.075f,-.5f,.5f,.005f,false},
        {"EyeRenderScale",L"Eye resolution scale",L"x",1.f,.25f,1.5f,.01f,true},
        {"ViewHudWidth",L"Text HUD size",L"x",.5f,.1f,1.f,.01f,true},
        {"ViewHudDistance",L"Text HUD distance",L"m",2.f,.5f,5.f,.05f,true},
        {"WristRadarSize",L"Watch diameter",L"m",.10f,.04f,.4f,.005f,true},
        {"WristRadarBackOffset",L"Watch toward elbow",L"m",.075f,-.25f,.5f,.005f,false},
        {"WristRadarLift",L"Watch above wrist",L"m",.04f,-.1f,.3f,.005f,false},
        {"DashboardRadarSize",L"Dashboard diameter",L"m",.16f,.04f,.5f,.005f,true},
        {"DashboardRadarForward",L"Dashboard forward",L"m",.55f,0.f,2.f,.01f,false},
        {"DashboardRadarDown",L"Dashboard down",L"m",.32f,-1.f,1.f,.01f,false},
        {"DashboardRadarRight",L"Dashboard right",L"m",.45f,-1.f,1.f,.01f,false},
        {"DashboardRadarTilt",L"Dashboard tilt",L"deg",12.f,-90.f,90.f,1.f,false},
        {"FirstPersonForwardOffset",L"Body behind eyes",L"m",.08f,-.5f,.5f,.005f,false}
    }};
    inline constexpr std::array<const char*,unsigned(Flag::Count)> flags{
        "DisableOpenXR","ExperimentalPhysicalCrouch","ShowFirstPersonBody","DiegeticAmmoCounters"};
    inline bool Valid(Number id,float value)
    {
        const auto index=unsigned(id);
        return index<numbers.size() && std::isfinite(value) && (!numbers[index].positive || value>0);
    }
    struct Values
    {
        std::array<float,unsigned(Number::Count)> values{};
        std::array<bool,unsigned(Flag::Count)> enabled{false,true,true,true};
        Values() {for(unsigned i=0;i<values.size();++i)values[i]=numbers[i].fallback;}
        float Get(Number id) const {return values[unsigned(id)];}
        bool Get(Flag id) const {return enabled[unsigned(id)];}
    };
    inline std::optional<float> FloorHeight(float left,float right,bool floor,bool tracked,bool fresh)
    {
        if(!floor || !tracked || !fresh || !std::isfinite(left) || !std::isfinite(right))return {};
        const float height=left*.5f+right*.5f;
        return std::isfinite(height) && height>0 ? std::optional<float>{height}:std::nullopt;
    }
}
