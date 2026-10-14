module;
#include <common.hxx>
#include "includes/vrsettingsmodel.hpp"
#include <atomic>
export module vrsettings;
import common;
import settings;
import vrconfig;
import openxrvr;
import vrcamera;
export namespace VRSettings
{
    void Install();
    std::string Status();
}
namespace
{
    using Screen=CSettings::MenuScreen;
    using Number=VRConfig::Number;
    using Flag=VRConfig::Flag;
    std::atomic<bool> registered{false};
    std::atomic<int> calibration{0};
    bool registrationStarted=false;
    std::atomic<int> resolutionApply{0};
    std::wstring NumberDescription(Number id)
    {
        switch(id)
        {
        case Number::PlayerHeight:
        {
            const auto height=VRConfig::Read().Get(id);
            return std::format(L"Standing eye height above the calibrated floor, in metres.~n~Crouch enters at {:.2f} m and exits at {:.2f} m. Use the separate seated offset in vehicles.",height*.65f,height*.75f);
        }
        case Number::EyeRenderScale:return L"Multiplies the headset's recommended width and height per eye. The desktop resolution is independent.~n~Select Apply VR resolution to recreate eye targets in this session.";
        case Number::HudWidth:return L"Physical size of the gameplay text and popup HUD. A scale of 0.5 gives a 2 metre wide plane. Moving it farther away makes it appear smaller. The pause-menu screen is separate.";
        case Number::HudDistance:return L"Distance of gameplay text, subtitles and popup prompts in front of your head, in metres. Size remains fixed as distance changes. This does not move the pause-menu screen.";
        case Number::WristSize:return L"Diameter of the radar watch on your left forearm, in metres.";
        case Number::WristBack:return L"Moves the watch from your wrist toward your elbow. Negative values move it toward the hand.";
        case Number::WristLift:return L"Moves the watch outward from the top of your wrist, in metres.";
        case Number::VehicleEyeHeight:return L"Vertical adjustment of the seated camera, in metres. Positive values raise your view inside the vehicle.";
        case Number::DashboardSize:return L"Diameter of the vehicle dashboard radar, in metres.";
        case Number::DashboardForward:return L"Moves the dashboard radar forward from the seated eye anchor, in metres.";
        case Number::DashboardDown:return L"Moves the dashboard radar down from the seated eye anchor, in metres.";
        case Number::DashboardRight:return L"Moves the dashboard radar to the right. Negative values move it left, in metres.";
        case Number::DashboardTilt:return L"Upward tilt of the dashboard radar toward the driver, in degrees.";
        case Number::BodyForward:return L"Moves your visible body behind the eye and controller origin, in metres. Tracking coordinates remain fixed.";
        default:return L"";
        }
    }
    bool Text(Screen screen,const char* key,std::function<std::wstring()> text)
    {return FusionFixSettings.SetOptionText(screen,key,std::move(text));}
    bool Button(Screen screen,const char* key,const wchar_t* label,std::function<void()> selected)
    {
        const bool result=FusionFixSettings.AddButton(screen,key,std::move(selected)) &&
            Text(screen,key,[label](){return std::wstring(label);});
        return result;
    }
    bool Numeric(Screen screen,Number id)
    {
        const auto& spec=VRSettingsModel::numbers[unsigned(id)];
        const auto key=std::format("NY_VR_N{:02}",unsigned(id));
        const bool result=FusionFixSettings.AddSlider(screen,key,spec.minimum,spec.maximum,spec.step,
            [id](){return VRConfig::Read().Get(id);},[id](float value){VRConfig::Set(id,value);}) &&
            FusionFixSettings.SetOptionText(screen,key,[id](){return std::wstring(VRSettingsModel::numbers[unsigned(id)].label);});
        if(!result)return false;
        std::function<std::wstring()> below;
        if(id==Number::EyeRenderScale)below=[](){
            unsigned width=0,height=0;
            if(!OpenXRVR::PreviewEyeResolution(VRConfig::Read().Get(Number::EyeRenderScale),width,height))return std::wstring(L"Headset resolution unavailable");
            return std::format(L"{} x {} per eye",width,height);
        };
        FusionFixSettings.SetOptionPresentation(screen,key,[id](){
            return std::format(L"{:.3g} {}",VRConfig::Read().Get(id),VRSettingsModel::numbers[unsigned(id)].unit);
        },[id](){return NumberDescription(id);},std::move(below));
        return result;
    }
    bool Toggle(Screen screen,Flag id,const wchar_t* label,bool inverted=false)
    {
        const auto key=std::format("NY_VR_B{:02}",unsigned(id));
        const bool result=FusionFixSettings.AddToggle(screen,key,
            [id,inverted](){return VRConfig::Read().Get(id)!=inverted;},
            [id,inverted](bool value){VRConfig::Set(id,value!=inverted);}) &&
            FusionFixSettings.SetOptionText(screen,key,[label](){return std::wstring(label);});
        FusionFixSettings.SetOptionPresentation(screen,key,{},[id](){
            switch(id)
            {
            case Flag::DisableOpenXR:return std::wstring(L"Enables VR on the next game launch. Switch off to launch flat screen. Changing this option does not end the current session.");
            case Flag::PhysicalCrouch:return std::wstring(L"Your tracked head height controls native crouching. Entry is 65% and standing is 75% of configured standing eye height.");
            case Flag::AmmoCounters:return std::wstring(L"Shows the round pins and reserve ammunition display attached to supported weapons. Individual weapon profiles retain their display settings when switched off.");
            case Flag::VehiclePanelEditing:return std::wstring(L"Grip the radar or radio to move and rotate it. Release to save placement for this vehicle model. With editing off, grip and drag the radio's station strip to tune the station in the centre.");
            default:return std::wstring(L"Displays the tracked first-person character body in VR.");
            }
        });
        return result;
    }
    void Register()
    {
        if(registered.load() || registrationStarted)return;
        const auto root=FusionFixSettings.AddScreen("VR",Screen::Graphics);
        if(root==Screen::Invalid)return;
        registrationStarted=true;
        bool ready=Toggle(root,Flag::DisableOpenXR,L"Enable VR on next launch",true);
        ready&=Numeric(root,Number::EyeRenderScale);
        ready&=FusionFixSettings.AddSpacer(root); // Space for the scale's resolution preview.
        ready&=Button(root,"NY_VR_APPLY",L"Apply VR resolution",[](){
            resolutionApply=OpenXRVR::RequestEyeRenderScale(VRConfig::Read().Get(Number::EyeRenderScale)) ? 1:-1;
        });
        ready&=FusionFixSettings.SetOptionPresentation(root,"NY_VR_APPLY",{},[](){
            return resolutionApply.load()<0 ? std::wstring(L"No active VR session. The scale is saved for the next launch."):
                std::wstring(L"Recreates headset eye targets and swapchains at the chosen scale. The pause menu may briefly disappear. Desktop video mode is retained.");
        });
        ready&=FusionFixSettings.AddSpacer(root);
        ready&=Button(root,"NY_VR_CENTER",L"Recenter headset",[](){VRCamera::Recenter();});
        ready&=FusionFixSettings.SetOptionPresentation(root,"NY_VR_CENTER",{},[](){return std::wstring(L"Resets the headset's game-facing reference on the next camera update. Standing-height calibration is retained.");});
        ready&=Numeric(root,Number::PlayerHeight);
        ready&=Button(root,"NY_VR_CAL",L"Calibrate standing eye height",[](){
            float height=0;
            if(!OpenXRVR::GetFloorHeight(height)){calibration=-1;return;}
            calibration=VRConfig::Set(Number::PlayerHeight,height) ? 1:-2;
        });
        ready&=FusionFixSettings.SetOptionPresentation(root,"NY_VR_CAL",{},[](){
            std::wstring result=L"Wear the headset and stand upright over a calibrated floor. Do not calibrate while seated or crouched.~n~";
            switch(calibration.load())
            {
            case 1:return result+L"Standing height saved.";
            case -1:return result+L"No fresh tracked floor pose; previous height retained.";
            case -2:return result+L"Could not save height; try again.";
            default:return result+L"Select to use the current floor-relative eye height.";
            }
        });
        ready&=Toggle(root,Flag::PhysicalCrouch,L"Physical crouch");
        ready&=Toggle(root,Flag::ShowBody,L"Show first-person body");
        ready&=Numeric(root,Number::BodyForward);
        ready&=FusionFixSettings.AddSpacer(root);
        for(auto id:{Number::HudWidth,Number::HudDistance,Number::WristSize,Number::WristBack,Number::WristLift})ready&=Numeric(root,id);
        ready&=Toggle(root,Flag::AmmoCounters,L"Diegetic ammo counters");
        ready&=FusionFixSettings.AddSpacer(root);
        const auto vehicle=FusionFixSettings.AddSubmenu(root,"Vehicle camera and radar");
        if(vehicle==Screen::Invalid)return;
        FusionFixSettings.SetSubmenuDescription(vehicle,[](){return std::wstring(L"Adjust the seated camera and the radar panel inside vehicles.");});
        for(auto id:{Number::VehicleEyeHeight,Number::DashboardSize,Number::DashboardForward,Number::DashboardDown,Number::DashboardRight,Number::DashboardTilt})ready&=Numeric(vehicle,id);
        ready&=Toggle(vehicle,Flag::VehiclePanelEditing,L"Grip to move vehicle panels");
        registered=ready;
    }
}
void VRSettings::Install()
{
    VRConfig::Initialize();
    FusionFix::onGameInitEvent()+=Register;
    // Also available when the frontend opens before the first playable frame.
    FusionFix::onMenuEnterEvent()+=Register;
}
std::string VRSettings::Status()
{
    const auto config=VRConfig::Read();
    return std::format("{{\"registered\":{},\"calibration\":{},\"playerHeight\":{},\"crouchEnter\":{},\"crouchExit\":{},\"nextLaunchEyeScale\":{},\"nextLaunchDisableOpenXR\":{}}}",
        registered.load(),calibration.load(),config.Get(Number::PlayerHeight),config.Get(Number::PlayerHeight)*.65f,
        config.Get(Number::PlayerHeight)*.75f,config.Get(Number::EyeRenderScale),config.Get(Flag::DisableOpenXR));
}
