module;
#include <common.hxx>
#include <wrl/client.h>
#include <d3dx9shader.h>
#include <d3dx9tex.h>
#include "includes/vrmath.hpp"
#include <atomic>
#include "includes/vrvehiclepanels.hpp"
#include <map>
#include <mutex>
#include <filesystem>
export module vrhud;
import common;
import vrdiagnostics;
import comvars;
import openxrvr;
import d3dx9_43;
import natives;
import settings;
import vrconfig;

export namespace VRHud
{
    void Install();
    void Reset();
    void Draw(IDirect3DDevice9* device,IDirect3DSurface9* eye,const VRMath::Matrix& viewProjection,const VRMath::Vector& eyePosition,const VRMath::Matrix* wristPose,const VRMath::Matrix* dashboardPose,IDirect3DSurface9* sceneDepth,const VRMath::Vector& logDepth,const D3DVIEWPORT9& depthViewport);
    std::string Status();
    void Capture();
    void CaptureRadar(IDirect3DDevice9* device,IDirect3DSurface9* source);
    bool PublishScreen(IDirect3DDevice9* device,bool loading);
    bool WantsScreen(bool loading);
    void SetOverlayRouting(bool (*route)(bool));
    void DrawOverlay(IDirect3DDevice9* device,IDirect3DSurface9* eye,const VRMath::Matrix& viewProjection,const VRMath::Matrix& headPose);
    void EndOverlayFrame();
    void AmmoChanged(); // Atomic request consumed by the native weapon-HUD updater.
    VRMath::Matrix PublishVehicle(unsigned model,const VRMath::Matrix& anchor,const VRMath::Matrix& vehicle,bool active);
    void InteractVehiclePanels(const VRMath::Matrix (&hands)[2],const VRMath::Matrix& vehicle,const OpenXRVR::ControllerInput& input,bool (&busy)[2]);
}

namespace
{
    using Microsoft::WRL::ComPtr;
    bool enabled=false;
    bool screens=true,dashboard=true;
    float dashboardSize=.16f,dashboardForward=.55f,dashboardDown=.32f,dashboardRight=.45f,dashboardTilt=12.f;
    using VehiclePanel=VRVehiclePanels::Panel;
    struct VehicleLayout {VehiclePanel panels[2];VRMath::Vector mount{};bool mountValid=false;bool custom[2]{},dirty=false;uint64_t changedAt=0;};
    std::mutex vehiclePanelLock;
    std::map<unsigned,VehicleLayout> vehicleLayouts;
    unsigned vehicleModel=0;
    VRMath::Matrix vehiclePanelAnchor=VRMath::Identity();
    VRMath::Matrix vehiclePanelVehicle=VRMath::Identity();
    uint64_t vehiclePanelAt=0;
    struct PanelGrip {int panel=-1;bool editing=false;VRMath::Matrix hand{},panelFrame{};VehiclePanel acquired{};float station=0;} panelGrips[2];
    bool panelGripPrevious[2]{true,true};
    std::atomic<int> requestedStation{-1};
    float radioCursor=20;
    unsigned radioStation=20;
    bool radioDragging=false;
    ComPtr<IDirect3DTexture9> radioImage;
    ComPtr<IDirect3DSurface9> radioSurface;
    bool radioFresh=false;uint64_t radioAt=0;
    float radioRect[4]{.15f,0.f,.70f,.18f};
    SafetyHookInline radioRender{},radioCarDraw{},radioCanDraw{};
    thread_local bool producingVehicleRadio=false;
    unsigned (__cdecl* nativeRadioCount)()=nullptr;
    void* (__cdecl* nativeRadioByIndex)(unsigned)=nullptr;
    float (__cdecl* nativeRadioWidth)(unsigned)=nullptr;
    void* (__cdecl* nativeRadioVehicle)(unsigned)=nullptr;
    std::atomic<bool> vehicleRadioAvailable=false;
    bool VehicleHasRadio()
    {
        auto* vehicle=nativeRadioVehicle ? static_cast<unsigned char*>(nativeRadioVehicle(0)):nullptr;
        // Native radio audio update (CE 009A3EF0): FE means no radio;
        // FF is merely switched off and must retain the station selector.
        return vehicle && vehicle[0xD10]!=0xFE;
    }
    unsigned* nativeRadioState=nullptr;unsigned* nativeRadioMode=nullptr;unsigned* nativeRadioStation=nullptr;
    float* nativeRadioScroll=nullptr;
    std::atomic<unsigned> radioCount{0},radioCaptures{0},radioDraws{0};
    std::string radioPlayingName,radioRequestedName;
    int radioPendingStation=-1;
    uint64_t radioRequestedAt=0;
    const char* NativeRadioName(unsigned station)
    {
        auto* object=nativeRadioByIndex ? nativeRadioByIndex(station):nullptr;
        return object ? static_cast<const char*>(object)+0x17FC:nullptr;
    }
    uint64_t stationPresses[2]{};
    bool vehicleProfileSaveOk=true;

    std::string VehicleProfile(unsigned model) {return std::format("VRVehicles/{:08X}.ini",model);}
    VehiclePanel DefaultVehiclePanel(unsigned kind)
    {
        return VRVehiclePanels::Default(dashboardRight,dashboardDown-(kind ? .19f:0.f),dashboardForward,
            dashboardTilt,kind ? .36f:dashboardSize,kind ? .09f:dashboardSize);
    }
    VehicleLayout& VehicleLayoutFor(unsigned model)
    {
        auto [it,added]=vehicleLayouts.try_emplace(model);
        auto& layout=it->second;
        if(added)
        {
            CIniReader ini(VehicleProfile(model));
            const char* mountKeys[]{"PositionX","PositionY","PositionZ"};
            layout.mount[3]=1;layout.mountValid=true;
            for(unsigned axis=0;axis<3;++axis)
            {
                const auto text=ini.ReadString("Mount",mountKeys[axis],"");char* end=nullptr;
                layout.mount[axis]=strtof(text.c_str(),&end);
                layout.mountValid &= end!=text.c_str() && *end==0 && std::isfinite(layout.mount[axis]);
            }
            for(unsigned kind=0;kind<2;++kind)
            {
                const char* section=kind ? "Radio":"Radar";
                auto p=DefaultVehiclePanel(kind);
                if(ini.ReadString(section,"PositionX","").empty()) {layout.panels[kind]=p;continue;}
                auto number=[&](const char* key) {
                    const auto text=ini.ReadString(section,key,"");char* end=nullptr;
                    const float value=strtof(text.c_str(),&end);
                    return end!=text.c_str() && *end==0 && std::isfinite(value) ? value:NAN;
                };
                float position[]{number("PositionX"),number("PositionY"),number("PositionZ")};
                float q[]{number("RotationX"),number("RotationY"),number("RotationZ"),number("RotationW")};
                const float norm=std::hypot(std::hypot(q[0],q[1]),std::hypot(q[2],q[3]));
                if(!std::isfinite(norm) || norm==0) {layout.panels[kind]=p;continue;}
                for(float& v:q)v/=norm;
                p.frame=VRMath::Pose(position,q);p.width=number("Width");p.height=number("Height");
                if(VRVehiclePanels::Valid(p)) {layout.panels[kind]=p;layout.custom[kind]=true;}
                else layout.panels[kind]=DefaultVehiclePanel(kind);
            }
        }
        for(unsigned kind=0;kind<2;++kind)if(!layout.custom[kind])layout.panels[kind]=DefaultVehiclePanel(kind);
        return layout;
    }
    void SaveVehicleLayouts()
    {
        std::lock_guard guard(vehiclePanelLock);
        for(auto& [model,layout]:vehicleLayouts)
        {
            if(!layout.dirty || GetTickCount64()-layout.changedAt<500)continue;
            try
            {
                CIniReader path(VehicleProfile(model));
                std::filesystem::create_directories(path.GetIniPath().parent_path());
                mINI::INIFile file(path.GetIniPath());mINI::INIStructure values;file.read(values);
                if(layout.mountValid)
                {
                    const char* positions[]{"PositionX","PositionY","PositionZ"};
                    for(unsigned axis=0;axis<3;++axis)values["Mount"][positions[axis]]=std::format("{:.9g}",layout.mount[axis]);
                }
                for(unsigned kind=0;kind<2;++kind)if(layout.custom[kind])
                {
                    auto& section=values[kind ? "Radio":"Radar"];
                    const auto& p=layout.panels[kind];const auto q=VRMath::Quaternion(p.frame);
                    const char* positions[]{"PositionX","PositionY","PositionZ"};
                    const char* rotations[]{"RotationX","RotationY","RotationZ","RotationW"};
                    for(unsigned a=0;a<3;++a)section[positions[a]]=std::format("{:.9g}",p.frame[3][a]);
                    for(unsigned a=0;a<4;++a)section[rotations[a]]=std::format("{:.9g}",q[a]);
                    section["Width"]=std::format("{:.9g}",p.width);section["Height"]=std::format("{:.9g}",p.height);
                }
                vehicleProfileSaveOk=file.write(values);
                if(vehicleProfileSaveOk)layout.dirty=false;else layout.changedAt=GetTickCount64();
            }
            catch(...) {vehicleProfileSaveOk=false;layout.changedAt=GetTickCount64();}
        }
    }
    std::atomic<unsigned> dashboardDraws{};
    SafetyHookMid radarHeadingHook{};
    std::atomic<float> carRadarHeading{},originalRadarHeading{},appliedRadarHeading{};
    std::atomic<uint64_t> carRadarAt{};
    std::atomic<unsigned> radarHeadingOverrides{};
    std::atomic<unsigned> wanted{};
    std::atomic<bool> wantedFlashing{};
    ComPtr<IDirect3DTexture9> screenTexture;
    ComPtr<IDirect3DSurface9> screenSurface;
    HANDLE screenShared=nullptr;
    unsigned screenWidth=0,screenHeight=0;
    float size=.10f,back=.075f,lift=.04f;
    SafetyHookInline radarDraw{};
    ComPtr<IDirect3DTexture9> radar;
    ComPtr<IDirect3DSurface9> radarSurface;
    ComPtr<IDirect3DPixelShader9> shader;
    uint64_t capturedAt=0;
    std::atomic<bool> captureRequested{};
    std::atomic<unsigned> calls{},queued{},executed{},captures{},draws{},failures{};
    std::atomic<float> lastBounds[4]{};
    std::atomic<float> lastPanel[4]{};
    struct Vertex {float x,y,z,rhw,u,v,clipW;};
    bool textHud=true,overlayFresh=false;
    bool (*overlayRouting)(bool)=nullptr;
    thread_local bool replayingOverlay=false;
    SafetyHookInline fontDraw{},popupSprite{},popupWindow{};
    SafetyHookInline hudText{},hudSprite{},weaponHudUpdate{};
    int* wantedFrontId=nullptr;int* wantedBackId=nullptr;int* ammoId=nullptr;int* weaponIconId=nullptr;
    unsigned (__cdecl* advanceHudFade)(int,unsigned)=nullptr;
    std::atomic<bool> ammoChanged{false};
    ComPtr<IDirect3DTexture9> overlayTexture;
    ComPtr<IDirect3DSurface9> overlaySurface;
    ComPtr<IDirect3DPixelShader9> overlayShader;
    std::atomic<unsigned> textCaptures{},textDraws{};
    std::atomic<unsigned> phoneCaptureTargets[3][6]{};
    void ObservePhoneTarget(unsigned kind)
    {
        if(!VRDiagnostics::counters) return;
        if(!CPhoneMgr::bDisplayMobile || !*CPhoneMgr::bDisplayMobile || replayingOverlay || !RageDirect3DDevice9::m_pRealDevice) return;
        auto* device=*RageDirect3DDevice9::m_pRealDevice;
        ComPtr<IDirect3DSurface9> target;
        D3DSURFACE_DESC desc{};D3DVIEWPORT9 viewport{};
        if(!device || FAILED(device->GetRenderTarget(0,&target)) || FAILED(target->GetDesc(&desc))) return;
        device->GetViewport(&viewport);
        auto* phone=rage::grcTextureFactoryPC::GetRTByName("PHONE_SCREEN");
        const unsigned values[]{static_cast<unsigned>(reinterpret_cast<uintptr_t>(target.Get())),desc.Width,desc.Height,
            phone && target.Get()==phone->mD3DSurface ? 1u:0u,viewport.Width,viewport.Height};
        for(unsigned i=0;i<6;++i) phoneCaptureTargets[kind][i]=values[i];
    }
    void** smsTexture=nullptr;
    ComPtr<IDirect3DTexture9> smsImage;
    ComPtr<IDirect3DSurface9> smsSurface;
    bool smsFresh=false;
    uint64_t smsAt=0;
    float smsBounds[4]{};
    std::atomic<unsigned> smsCaptures{};
    float hudWidth=.64f,hudDistance=2.f;
    unsigned hudConfigRevision=~0u;
    void ApplySettings()
    {
        const auto revision=VRConfig::Revision();
        if(hudConfigRevision==revision)return;
        const auto config=VRConfig::Read();
        using Number=VRConfig::Number;
        hudWidth=config.Get(Number::HudWidth);hudDistance=config.Get(Number::HudDistance);
        size=config.Get(Number::WristSize);back=config.Get(Number::WristBack);lift=config.Get(Number::WristLift);
        dashboardSize=config.Get(Number::DashboardSize);dashboardForward=config.Get(Number::DashboardForward);
        dashboardDown=config.Get(Number::DashboardDown);dashboardRight=config.Get(Number::DashboardRight);
        dashboardTilt=config.Get(Number::DashboardTilt);hudConfigRevision=revision;
    }

    bool Allowed()
    {
        return (enabled || textHud) && OpenXRVR::HasFocusedViews() && CTimer::ms_bUserPause && CTimer::ms_bScriptPause &&
            !*CTimer::ms_bUserPause && !*CTimer::ms_bScriptPause;
    }

    bool SuppressHudRecord(int id)
    {
        const bool stars=(wantedFrontId && id==*wantedFrontId) || (wantedBackId && id==*wantedBackId);
        const bool weapon=(ammoId && id==*ammoId) || (weaponIconId && id==*weaponIconId);
        return (stars || (weapon && VRConfig::Read().Get(VRConfig::Flag::AmmoCounters))) && Allowed();
    }
    void __cdecl HudText(int id,unsigned mode)
    {
        if(SuppressHudRecord(id)) {advanceHudFade(id,mode);return;}
        hudText.unsafe_ccall(id,mode);
    }
    void __cdecl HudSprite(int id,unsigned mode)
    {
        if(SuppressHudRecord(id)) {advanceHudFade(id,mode);return;}
        hudSprite.unsafe_ccall(id,mode);
    }
    void __cdecl WeaponHudUpdate()
    {
        if(ammoChanged.exchange(false) && Allowed() && !VRConfig::Read().Get(VRConfig::Flag::AmmoCounters) &&
            NativeInvoke::GetNativeHandler(std::to_underlying(Natives::NativeHashes::DISPLAY_AMMO)))
            Natives::DisplayAmmo(true);
        weaponHudUpdate.unsafe_ccall();
    }
    int* FindHudSlot(const char* signature,const char* name,unsigned slotOffset)
    {
        auto matches=hook::pattern(signature);
        for(size_t i=0;i<matches.size();++i)
        {
            auto* code=matches.get(i).get<unsigned char>();
            const auto* label=*reinterpret_cast<const char* const*>(code+3);
            MEMORY_BASIC_INFORMATION region{};
            const auto length=std::strlen(name)+1;
            if(!VirtualQuery(label,&region,sizeof(region)) || region.State!=MEM_COMMIT ||
                (region.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
                reinterpret_cast<uintptr_t>(label)+length>reinterpret_cast<uintptr_t>(region.BaseAddress)+region.RegionSize)continue;
            if(std::memcmp(label,name,length)==0)return *reinterpret_cast<int**>(code+slotOffset);
        }
        return nullptr;
    }

    // Replay resolved drawing only: never advance the HUD records or consume
    // a render-command stream twice. The desktop pass still executes once.
    template<class Draw> void CaptureOverlay(Draw&& draw,unsigned kind=0)
    {
        const bool sms=kind==1,radio=kind==2;
        if(((sms || radio) ? !enabled:!textHud) || replayingOverlay || !Allowed() || !overlayRouting ||
            !RageDirect3DDevice9::m_pRealDevice || !overlayRouting(true)) return;
        auto& texture=radio ? radioImage:(sms ? smsImage:overlayTexture);
        auto& surface=radio ? radioSurface:(sms ? smsSurface:overlaySurface);
        auto& fresh=radio ? radioFresh:(sms ? smsFresh:overlayFresh);
        auto* device=*RageDirect3DDevice9::m_pRealDevice;
        // PHONE_SCREEN already supplies the native held phone. Capturing its
        // offscreen text/sprites here duplicates them on the view HUD plane.
        ComPtr<IDirect3DSurface9> sourceTarget;
        auto* phone=rage::grcTextureFactoryPC::GetRTByName("PHONE_SCREEN");
        if(phone && device && SUCCEEDED(device->GetRenderTarget(0,&sourceTarget)) &&
            sourceTarget.Get()==phone->mD3DSurface)
        {overlayRouting(false);return;}
        // Offscreen script targets (including phone targets created before the
        // factory cache hook) are not desktop HUD content. Do not stretch their
        // square UI into the view plane merely because the same font/sprite
        // commands are used. Native rendering above remains untouched.
        ComPtr<IDirect3DSurface9> backBuffer;
        D3DSURFACE_DESC sourceDesc{},backDesc{};
        if(device && SUCCEEDED(device->GetRenderTarget(0,&sourceTarget)) &&
            SUCCEEDED(sourceTarget->GetDesc(&sourceDesc)) &&
            SUCCEEDED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backBuffer)) &&
            SUCCEEDED(backBuffer->GetDesc(&backDesc)) &&
            (sourceDesc.Width!=backDesc.Width || sourceDesc.Height!=backDesc.Height))
        {overlayRouting(false);return;}
        ComPtr<IDirect3DStateBlock9> state;
        ComPtr<IDirect3DSurface9> targets[4],depth;
        D3DVIEWPORT9 viewport{};
        D3DSURFACE_DESC desc{};
        bool bound=false;
        if(device && SUCCEEDED(device->GetRenderTarget(0,&targets[0])) &&
            SUCCEEDED(targets[0]->GetDesc(&desc)) &&
            SUCCEEDED(device->CreateStateBlock(D3DSBT_ALL,&state)) && SUCCEEDED(state->Capture()))
        {
            for(unsigned i=1;i<4;++i) device->GetRenderTarget(i,&targets[i]);
            device->GetDepthStencilSurface(&depth);device->GetViewport(&viewport);
            D3DSURFACE_DESC previous{};
            if(surface && (FAILED(surface->GetDesc(&previous)) ||
                previous.Width!=desc.Width || previous.Height!=desc.Height))
            {surface.Reset();texture.Reset();fresh=false;}
            if(!texture && SUCCEEDED(device->CreateTexture(desc.Width,desc.Height,1,D3DUSAGE_RENDERTARGET,
                D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&texture,nullptr)))
                texture->GetSurfaceLevel(0,&surface);
            if(surface && (fresh || SUCCEEDED(device->ColorFill(surface.Get(),nullptr,0))))
            {
                for(unsigned i=1;i<4;++i) device->SetRenderTarget(i,nullptr);
                device->SetDepthStencilSurface(nullptr);
                bound=SUCCEEDED(device->SetRenderTarget(0,surface.Get()));
                if(bound)
                {
                    device->SetRenderState(D3DRS_COLORWRITEENABLE,15);
                    device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,TRUE);
                    device->SetRenderState(D3DRS_SRCBLENDALPHA,D3DBLEND_ONE);
                    device->SetRenderState(D3DRS_DESTBLENDALPHA,D3DBLEND_INVSRCALPHA);
                    device->SetRenderState(D3DRS_BLENDOPALPHA,D3DBLENDOP_ADD);
                    replayingOverlay=true;draw();replayingOverlay=false;
                    fresh=true;
                    if(radio) {radioAt=GetTickCount64();VRDiagnostics::Increment(radioCaptures);}
                    else if(sms) {smsAt=GetTickCount64();VRDiagnostics::Increment(smsCaptures);} else VRDiagnostics::Increment(textCaptures);
                }
            }
            for(unsigned i=0;i<4;++i) device->SetRenderTarget(i,targets[i].Get());
            device->SetDepthStencilSurface(depth.Get());state->Apply();device->SetViewport(&viewport);
        }
        if(!bound) VRDiagnostics::Increment(failures);
        overlayRouting(false);
    }

    void __cdecl FontDraw(const void* begin,const void* end)
    {
        ObservePhoneTarget(0);
        fontDraw.unsafe_ccall(begin,end);
        if(begin!=end) CaptureOverlay([&](){fontDraw.unsafe_ccall(begin,end);});
    }
    void __fastcall PopupSprite(void* self,void* edx)
    {
        ObservePhoneTarget(1);
        popupSprite.unsafe_fastcall(self,edx);
        const auto* data=static_cast<const unsigned char*>(self);
        auto* phone=rage::grcTextureFactoryPC::GetRTByName("PHONE_SCREEN");
        if(phone && *reinterpret_cast<void* const*>(data+0x18)==phone) return;
        if(enabled && smsTexture && *smsTexture && *reinterpret_cast<void* const*>(data+0x18)==*smsTexture &&
            *reinterpret_cast<const int*>(data+0x24)==2)
        {
            const auto* bounds=reinterpret_cast<const float*>(data+8);
            if(std::isfinite(bounds[0]+bounds[1]+bounds[2]+bounds[3]) && bounds[2]>0 && bounds[3]>0)
            {
                std::copy_n(bounds,4,smsBounds);
                CaptureOverlay([&](){popupSprite.unsafe_fastcall(self,edx);},true);
            }
            return;
        }
        CaptureOverlay([&](){popupSprite.unsafe_fastcall(self,edx);});
    }
    void __fastcall PopupWindow(void* self,void* edx)
    {
        ObservePhoneTarget(2);
        popupWindow.unsafe_fastcall(self,edx);
        CaptureOverlay([&](){popupWindow.unsafe_fastcall(self,edx);});
    }

    void __fastcall RadioRender(void* self,void* edx)
    {
        radioRender.unsafe_fastcall(self,edx);
        {std::lock_guard guard(vehiclePanelLock);if(!vehiclePanelAt || GetTickCount64()-vehiclePanelAt>150)return;}
        CaptureOverlay([&](){radioRender.unsafe_fastcall(self,edx);},2);
    }
    bool __cdecl RadioCanDraw(unsigned viewport)
    {
        if(producingVehicleRadio)return vehicleRadioAvailable.load();
        return radioCanDraw.unsafe_ccall<bool>(viewport);
    }
    void __cdecl RadioCarDraw(unsigned viewport)
    {
        unsigned station=0;float cursor=0;bool dragging=false,active=false;
        {std::lock_guard guard(vehiclePanelLock);station=radioStation;cursor=radioCursor;dragging=radioDragging;
            active=vehiclePanelAt && GetTickCount64()-vehiclePanelAt<150;}
        vehicleRadioAvailable=active && VehicleHasRadio();
        if(!active || !vehicleRadioAvailable || !dashboard || !Allowed() || !nativeRadioCount)
        {radioCarDraw.unsafe_ccall(viewport);return;}
        const unsigned count=nativeRadioCount();radioCount=count;
        if(!count || station>=count) {radioCarDraw.unsafe_ccall(viewport);return;}
        // Scope presentation overrides to this native producer only. Simulation,
        // retuning, frontend state and queued render commands retain their owners.
        const unsigned state=*nativeRadioState,mode=*nativeRadioMode,selected=*nativeRadioStation;
        const float scroll=*nativeRadioScroll;
        *nativeRadioMode=3;*nativeRadioStation=station;*nativeRadioState=dragging ? 4:3;
        float residual=cursor-float(station);
        if(residual>count*.5f)residual-=float(count);
        if(residual<-float(count)*.5f)residual+=float(count);
        *nativeRadioScroll=dragging ? -residual*(nativeRadioWidth(3)+.003f):0;
        producingVehicleRadio=true;radioCarDraw.unsafe_ccall(viewport);producingVehicleRadio=false;
        *nativeRadioState=state;*nativeRadioMode=mode;*nativeRadioStation=selected;*nativeRadioScroll=scroll;
    }

    uint64_t nativeRadarAt=0;
    void __cdecl RadarDraw()
    {
        VRDiagnostics::Increment(calls);
        radarDraw.unsafe_ccall();
        if(Allowed() && RageDirect3DDevice9::m_pRealDevice)
        {
            auto* device=*RageDirect3DDevice9::m_pRealDevice;
            D3DVIEWPORT9 viewport{};
            if(device && SUCCEEDED(device->GetViewport(&viewport)))
            {
                lastBounds[0]=static_cast<float>(viewport.X); lastBounds[1]=static_cast<float>(viewport.Y);
                lastBounds[2]=static_cast<float>(viewport.X+viewport.Width);lastBounds[3]=static_cast<float>(viewport.Y+viewport.Height);
                nativeRadarAt=GetTickCount64();
            }
        }
    }

    bool CreateShader(IDirect3DDevice9* device)
    {
        if(shader) return true;
        constexpr char code[]=R"(
            sampler2D radar : register(s0);
            sampler2D sms : register(s1);
            float4 wantedState : register(c0);
            float4 smsRect : register(c1);
            float4 logDepth : register(c2);
            float4 radioState : register(c3);
            float4 Shade(float2 uv) {
                if(wantedState.w>1.5) {
                    float4 image=tex2D(radar,radioState.xy+uv*radioState.zw);
                    clip(image.a-.01);
                    return float4(image.rgb/max(image.a,.01),image.a);
                }
                if(uv.y>1) {
                    clip(wantedState.z-.5);
                    float2 p=float2((uv.x-.35)/.30,(uv.y-1.05)/.25);
                    clip(p);clip(1-p);
                    float4 icon=tex2D(sms,smsRect.xy+p*smsRect.zw);
                    clip(icon.a-.03);
                    return float4(icon.rgb/max(icon.a,.03),icon.a);
                }
                if(uv.y<0) {
                    float count=wantedState.x;
                    clip(count-.5);
                    float x=(uv.x-.5)*6+count*.5;
                    clip(x); clip(count-x);
                    float2 p=float2(frac(x)-.5,(uv.y+.15)*6);
                    float a=atan2(p.x,-p.y)+6.28318530;
                    float sector=a/0.62831853;
                    float k=floor(sector),t=frac(sector)*0.62831853;
                    float r0=fmod(k,2)<.5 ? .43:.18;
                    float r1=fmod(k,2)<.5 ? .18:.43;
                    float radius=r0*r1*.58778525/(r1*sin(.62831853-t)+r0*sin(t));
                    clip(radius-length(p));
                    return float4(wantedState.yyy,1);
                }
                float r=length(uv-float2(.5,.5));
                clip(.5-r);
                if(r>.48) return float4(.035,.035,.04,1);
                return float4(tex2D(radar,uv).rgb,1);
            }
            struct Output {float4 color:COLOR0;float depth:DEPTH;};
            Output main(float2 uv:TEXCOORD0,float clipW:TEXCOORD1) {
                Output o;o.color=Shade(uv);
                o.depth=wantedState.w>.5 ? saturate(log2(clipW*logDepth.x)*logDepth.y):0;
                return o;
            })";
        ComPtr<ID3DXBuffer> compiled,errors;
        if(FAILED(D3DXCompileShader(code,sizeof(code)-1,nullptr,nullptr,"main","ps_3_0",0,&compiled,&errors,nullptr)) ||
            FAILED(device->CreatePixelShader(static_cast<const DWORD*>(compiled->GetBufferPointer()),&shader)))
        {VRDiagnostics::Increment(failures); return false;}
        return true;
    }
}

VRMath::Matrix VRHud::PublishVehicle(unsigned model,const VRMath::Matrix& anchor,const VRMath::Matrix& vehicle,bool active)
{
    std::lock_guard guard(vehiclePanelLock);
    if(!active)
    {
        vehiclePanelAt=0;radioDragging=false;radioPendingStation=-1;requestedStation=-1;
        for(auto& grip:panelGrips)grip={};
        panelGripPrevious[0]=panelGripPrevious[1]=true;return anchor;
    }
    if(vehicleModel!=model)
    {
        for(auto& grip:panelGrips)grip={};radioDragging=false;
        radioPendingStation=-1;requestedStation=-1;
        panelGripPrevious[0]=panelGripPrevious[1]=true;
    }
    auto& layout=VehicleLayoutFor(model);
    if(!layout.mountValid)
    {
        // Preserve legacy panel offsets by recording their original seat reference
        // once. Subsequent entries must not recalibrate the vehicle panel mount.
        layout.mount=VRVehiclePanels::MountOffset(anchor,vehicle[3]);
        layout.mountValid=std::isfinite(layout.mount[0]) && std::isfinite(layout.mount[1]) && std::isfinite(layout.mount[2]);
        if(layout.mountValid){layout.dirty=true;layout.changedAt=GetTickCount64();}
    }
    const auto fixed=layout.mountValid ? VRVehiclePanels::FixedMount(anchor,vehicle[3],layout.mount):anchor;
    vehicleModel=model;vehiclePanelAnchor=fixed;vehiclePanelVehicle=vehicle;vehiclePanelAt=GetTickCount64();
    return fixed;
}
void VRHud::InteractVehiclePanels(const VRMath::Matrix (&hands)[2],const VRMath::Matrix& vehicle,const OpenXRVR::ControllerInput& input,bool (&busy)[2])
{
    const bool editing=VRConfig::Read().Get(VRConfig::Flag::VehiclePanelEditing);
    std::lock_guard guard(vehiclePanelLock);
    if(!dashboard || !Allowed() || !vehiclePanelAt || GetTickCount64()-vehiclePanelAt>150)return;
    auto& layout=VehicleLayoutFor(vehicleModel);
    const unsigned stationCount=radioCount.load();
    const auto currentAnchor=VRMath::Multiply(VRMath::Multiply(vehiclePanelAnchor,VRMath::InverseRigid(vehiclePanelVehicle)),vehicle);
    const auto inverse=VRMath::InverseRigid(currentAnchor);
    const bool wasDragging=radioDragging;radioDragging=false;
    for(unsigned hand=0;hand<2;++hand)
    {
        auto& grip=panelGrips[hand];
        const bool pressed=input.gripActive[hand] && input.gripped[hand];
        const bool edge=pressed && !panelGripPrevious[hand];panelGripPrevious[hand]=pressed;
        if(!pressed || !input.gripPoseValid[hand] || input.phoneActive || !input.driving || (grip.panel==1 && !vehicleRadioAvailable) || (grip.panel>=0 && grip.editing!=editing))
        {grip={};continue;}
        const auto localHand=VRMath::Multiply(hands[hand],inverse);
        if(grip.panel<0 && edge && !busy[hand])
        {
            float nearest=.12f;int selected=-1;
            for(unsigned kind=editing ? 0:1;kind<2;++kind)
            {
                if(kind==1 && !vehicleRadioAvailable)continue;
                if(panelGrips[1-hand].panel==int(kind))continue;
                const float distance=VRVehiclePanels::Distance(layout.panels[kind],localHand[3]);
                if(distance<nearest){nearest=distance;selected=int(kind);}
            }
            if(selected>=0)
            {
                grip.panel=selected;grip.editing=editing;grip.hand=localHand;
                grip.acquired=layout.panels[selected];grip.panelFrame=grip.acquired.frame;
                grip.station=float(radioStation);
            }
        }
        if(grip.panel<0)continue;
        busy[hand]=true;
        if(editing)
        {
            auto next=grip.acquired;next.frame=VRVehiclePanels::Drag(grip.panelFrame,grip.hand,localHand);
            if(VRVehiclePanels::Valid(next))
            {
                layout.panels[grip.panel]=next;layout.custom[grip.panel]=true;
                layout.dirty=true;layout.changedAt=GetTickCount64();
            }
        }
        else
        {
            if(!stationCount) {grip={};continue;}
            radioDragging=true;
            const float position=grip.station+VRVehiclePanels::StationsDragged(grip.acquired,grip.hand[3],localHand[3]);
            if(!std::isfinite(position))continue;
            radioCursor=position-std::floor(position/stationCount)*stationCount;
            const unsigned selected=unsigned(std::floor(radioCursor+.5f))%stationCount;
            if(selected!=radioStation){radioStation=selected;requestedStation=int(selected);}
        }
    }
    if(!radioDragging)
    {
        radioCursor=float(radioStation);
        if(wasDragging)requestedStation=int(radioStation);
    }
}

void VRHud::CaptureRadar(IDirect3DDevice9* device,IDirect3DSurface9* source)
{
    if(!Allowed() || !nativeRadarAt || GetTickCount64()-nativeRadarAt>250) return;
    VRDiagnostics::Increment(queued); VRDiagnostics::Increment(executed);
    D3DSURFACE_DESC desc{}; if(FAILED(source->GetDesc(&desc))) return;
    RECT rect{static_cast<LONG>(lastBounds[0].load()),static_cast<LONG>(lastBounds[1].load()),
        static_cast<LONG>(lastBounds[2].load()),static_cast<LONG>(lastBounds[3].load())};
    rect.left=std::clamp(rect.left,0L,static_cast<LONG>(desc.Width));rect.right=std::clamp(rect.right,0L,static_cast<LONG>(desc.Width));
    rect.top=std::clamp(rect.top,0L,static_cast<LONG>(desc.Height));rect.bottom=std::clamp(rect.bottom,0L,static_cast<LONG>(desc.Height));
    if(rect.right-rect.left<16 || rect.bottom-rect.top<16) return;
    if(!radarSurface)
    {
        radar.Reset();
        if(FAILED(device->CreateTexture(512,512,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&radar,nullptr)) ||
            FAILED(radar->GetSurfaceLevel(0,&radarSurface))) {VRDiagnostics::Increment(failures);return;}
    }
    if(SUCCEEDED(device->StretchRect(source,&rect,radarSurface.Get(),nullptr,D3DTEXF_LINEAR)))
    {capturedAt=GetTickCount64();VRDiagnostics::Increment(captures);} else VRDiagnostics::Increment(failures);
}

static void DrawPanel(IDirect3DDevice9* device,IDirect3DSurface9* eye,const VRMath::Matrix& vp,const VRMath::Vector& eyePosition,const VRMath::Matrix* wristPose,const VRMath::Matrix* dashboardPose,IDirect3DSurface9* sceneDepth,const VRMath::Vector& logDepth,const D3DVIEWPORT9& depthViewport,bool radioPanel)
{
    ApplySettings();
    const bool inCar=dashboard && dashboardPose;
    if(!inCar && !wristPose) return;
    if(!Allowed() || (radioPanel ? (!inCar || !vehicleRadioAvailable || !radioImage || !radioAt || GetTickCount64()-radioAt>250):(!radar || !capturedAt || GetTickCount64()-capturedAt>250)) ||
        !CreateShader(device)) return;
    const auto& mount=inCar ? *dashboardPose:*wristPose;
    D3DSURFACE_DESC desc{};
    if(FAILED(eye->GetDesc(&desc))) return;
    if(inCar)
    {
        D3DSURFACE_DESC depthDesc{};
        if(!sceneDepth || FAILED(sceneDepth->GetDesc(&depthDesc)) ||
            depthDesc.Width!=desc.Width || depthDesc.Height!=desc.Height ||
            !std::isfinite(logDepth[0]+logDepth[1]) || logDepth[0]<=0 || logDepth[1]<=0) return;
    }
    VehiclePanel panel=DefaultVehiclePanel(radioPanel ? 1:0);float cursor=0;
    if(inCar){std::lock_guard guard(vehiclePanelLock);panel=VehicleLayoutFor(vehicleModel).panels[radioPanel ? 1:0];cursor=radioCursor;}
    const auto panelWorld=VRMath::Multiply(panel.frame,mount);
    const auto center=VRMath::Transform(inCar ? panel.frame[3]:
        VRMath::Vector{-back,-lift,0,1},mount);
    VRMath::Vector normal{};
    for(unsigned a=0;a<3;++a)normal[a]=inCar ? panelWorld[2][a]:-mount[1][a];
    if(VRMath::Dot3(normal,VRMath::Difference3(eyePosition,center))<=0) return;
    Vertex vertices[4];
    const bool smsVisible=!radioPanel && smsImage && smsAt && GetTickCount64()-smsAt<250;
    for(unsigned i=0;i<4;++i)
    {
        const float u=(i&1) ? 1.f:0.f,v=(i&2) ? (smsVisible ? 1.30f:1.f):(!radioPanel && wanted.load() ? -.30f:0.f);
        VRMath::Vector local{};
        if(inCar)
        {
            local=VRMath::Transform({(u-.5f)*panel.width,(.5f-v)*panel.height,0,1},panel.frame);
        }
        else local={-back+(0.5f-v)*size,-lift,(u-0.5f)*size,1};
        const auto world=VRMath::Transform(local,mount);
        const auto clip=VRMath::Transform(world,vp);
        if(!std::isfinite(clip[0]+clip[1]+clip[2]+clip[3]) || clip[3]<.03f || clip[2]<0) return;
        const float rhw=1.f/clip[3];
        vertices[i]={(clip[0]*rhw+1)*desc.Width*.5f-.5f,(1-clip[1]*rhw)*desc.Height*.5f-.5f,
            // Viewed from the -Y mount face, increasing local Z goes left.
            clip[2]*rhw,rhw,inCar ? u:1.f-u,v,clip[3]};
    }
    for(unsigned i=0;i<4;++i) {if(VRDiagnostics::counters) lastPanel[i]=(i&1) ? vertices[i/2].y:vertices[i/2].x;}
    ComPtr<IDirect3DStateBlock9> state;
    ComPtr<IDirect3DSurface9> targets[4],depth;
    D3DVIEWPORT9 viewport{};
    if(FAILED(device->CreateStateBlock(D3DSBT_ALL,&state)) || FAILED(state->Capture())) return;
    for(unsigned i=0;i<4;++i) device->GetRenderTarget(i,&targets[i]);
    device->GetDepthStencilSurface(&depth);device->GetViewport(&viewport);
    for(unsigned i=1;i<4;++i) device->SetRenderTarget(i,nullptr);
    device->SetDepthStencilSurface(nullptr);device->SetRenderTarget(0,eye);
    if(inCar && FAILED(device->SetDepthStencilSurface(sceneDepth)))
    {
        for(unsigned i=0;i<4;++i) device->SetRenderTarget(i,targets[i].Get());
        device->SetDepthStencilSurface(depth.Get());state->Apply();device->SetViewport(&viewport);return;
    }
    const D3DVIEWPORT9 panelViewport{0,0,desc.Width,desc.Height,inCar ? depthViewport.MinZ:0.f,inCar ? depthViewport.MaxZ:1.f}; device->SetViewport(&panelViewport);
    device->SetVertexShader(nullptr);device->SetPixelShader(shader.Get());
    const float brightness=wantedFlashing.load() && (GetTickCount64()/300)%2 ? .35f:1.f;
    const float wantedState[4]{static_cast<float>(wanted.load()),brightness,smsVisible ? 1.f:0.f,radioPanel ? 2.f:(inCar ? 1.f:0.f)};
    device->SetPixelShaderConstantF(0,wantedState,1);
    device->SetPixelShaderConstantF(1,smsBounds,1);
    device->SetPixelShaderConstantF(2,logDepth.data(),1);
    device->SetPixelShaderConstantF(3,radioRect,1);
    device->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX2|D3DFVF_TEXCOORDSIZE1(1));device->SetTexture(0,radioPanel ? radioImage.Get():radar.Get());
    device->SetTexture(1,smsImage.Get());
    device->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);
    device->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
    device->SetRenderState(D3DRS_FOGENABLE,FALSE);
    device->SetRenderState(D3DRS_CLIPPLANEENABLE,0);
    device->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);
    device->SetRenderState(D3DRS_ZENABLE,inCar);device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
    device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
    device->SetRenderState(D3DRS_DEPTHBIAS,0);device->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,0);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
    device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE,FALSE);device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);device->SetRenderState(D3DRS_COLORWRITEENABLE,15);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
    device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
    device->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);device->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
    device->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);device->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
    device->SetSamplerState(1,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);device->SetSamplerState(1,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
    device->SetSamplerState(1,D3DSAMP_MIPFILTER,D3DTEXF_NONE);device->SetSamplerState(1,D3DSAMP_SRGBTEXTURE,FALSE);
    device->SetSamplerState(1,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);device->SetSamplerState(1,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
    if(SUCCEEDED(device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex)))) {VRDiagnostics::Increment(draws);if(inCar)VRDiagnostics::Increment(dashboardDraws);if(radioPanel)VRDiagnostics::Increment(radioDraws);} else VRDiagnostics::Increment(failures);
    device->SetDepthStencilSurface(nullptr);
    for(unsigned i=0;i<4;++i) device->SetRenderTarget(i,targets[i].Get());
    device->SetDepthStencilSurface(depth.Get());state->Apply();device->SetViewport(&viewport);
    if(!radioPanel && captureRequested.exchange(false))
    {
        wchar_t path[MAX_PATH]{}; GetModuleFileNameW(nullptr,path,MAX_PATH);
        auto* name=wcsrchr(path,L'\\'); if(name) name[1]=0;
        const auto folder=std::wstring(path)+L"plugins\\";
        if(FAILED(D3DXSaveSurfaceToFileW((folder+L"GTAIV.VR.wrist-radar.png").c_str(),D3DXIFF_PNG,radarSurface.Get(),nullptr,nullptr)) ||
            FAILED(D3DXSaveSurfaceToFileW((folder+L"GTAIV.VR.wrist-eye.png").c_str(),D3DXIFF_PNG,eye,nullptr,nullptr))) VRDiagnostics::Increment(failures);
    }
}

void VRHud::Draw(IDirect3DDevice9* device,IDirect3DSurface9* eye,const VRMath::Matrix& vp,const VRMath::Vector& eyePosition,const VRMath::Matrix* wristPose,const VRMath::Matrix* dashboardPose,IDirect3DSurface9* sceneDepth,const VRMath::Vector& logDepth,const D3DVIEWPORT9& depthViewport)
{
    DrawPanel(device,eye,vp,eyePosition,wristPose,dashboardPose,sceneDepth,logDepth,depthViewport,false);
    if(dashboard && dashboardPose)DrawPanel(device,eye,vp,eyePosition,wristPose,dashboardPose,sceneDepth,logDepth,depthViewport,true);
}
void VRHud::Capture() {captureRequested=true;}

void VRHud::SetOverlayRouting(bool (*route)(bool)) {overlayRouting=route;}
void VRHud::EndOverlayFrame() {overlayFresh=false;smsFresh=false;radioFresh=false;}
void VRHud::AmmoChanged() {ammoChanged=true;}

void VRHud::DrawOverlay(IDirect3DDevice9* device,IDirect3DSurface9* eye,const VRMath::Matrix& vp,const VRMath::Matrix& headPose)
{
    ApplySettings();
    if(!textHud || !Allowed() || !overlayFresh || !overlayTexture || !eye) return;
    if(!overlayShader)
    {
        constexpr char code[]="sampler2D image:register(s0); float4 main(float2 uv:TEXCOORD0):COLOR0{return tex2D(image,uv);}";
        ComPtr<ID3DXBuffer> compiled,errors;
        if(FAILED(D3DXCompileShader(code,sizeof(code)-1,nullptr,nullptr,"main","ps_3_0",0,&compiled,&errors,nullptr)) ||
            FAILED(device->CreatePixelShader(static_cast<const DWORD*>(compiled->GetBufferPointer()),&overlayShader))) {VRDiagnostics::Increment(failures);return;}
    }
    D3DSURFACE_DESC desc{};
    ComPtr<IDirect3DStateBlock9> state;
    ComPtr<IDirect3DSurface9> targets[4],depth;
    D3DVIEWPORT9 viewport{};
    if(FAILED(eye->GetDesc(&desc))) return;
    D3DSURFACE_DESC hudDesc{};
    if(FAILED(overlaySurface->GetDesc(&hudDesc)) || !hudDesc.Width) return;
    // Keep the plane's physical size independent of distance. At the original
    // 2 m default this preserves existing width tuning; moving it farther away
    // now makes it visibly smaller instead of enlarging it to cancel the move.
    const float width=4.f*hudWidth,height=width*hudDesc.Height/hudDesc.Width;
    Vertex vertices[4];
    for(unsigned i=0;i<4;++i)
    {
        const float u=(i&1) ? 1.f:0.f,v=(i&2) ? 1.f:0.f;
        // One shared plane, projected separately through the same eye cameras
        // as the scene. Identical eye pixel coordinates are not a stereo HUD.
        const auto world=VRMath::Transform({(u-.5f)*width,(.5f-v)*height,-hudDistance,1},headPose);
        const auto clip=VRMath::Transform(world,vp);
        if(!std::isfinite(clip[0]+clip[1]+clip[2]+clip[3]) || clip[3]<.03f || clip[2]<0) return;
        const float rhw=1.f/clip[3];
        vertices[i]={(clip[0]*rhw+1)*desc.Width*.5f-.5f,(1-clip[1]*rhw)*desc.Height*.5f-.5f,clip[2]*rhw,rhw,u,v};
    }
    if(FAILED(device->CreateStateBlock(D3DSBT_ALL,&state)) || FAILED(state->Capture())) return;
    for(unsigned i=0;i<4;++i) device->GetRenderTarget(i,&targets[i]);
    device->GetDepthStencilSurface(&depth);device->GetViewport(&viewport);
    for(unsigned i=1;i<4;++i) device->SetRenderTarget(i,nullptr);
    device->SetDepthStencilSurface(nullptr);device->SetRenderTarget(0,eye);
    const D3DVIEWPORT9 full{0,0,desc.Width,desc.Height,0,1};device->SetViewport(&full);
    device->SetVertexShader(nullptr);device->SetPixelShader(overlayShader.Get());
    device->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);device->SetTexture(0,overlayTexture.Get());
    device->SetRenderState(D3DRS_ZENABLE,FALSE);device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
    device->SetRenderState(D3DRS_FOGENABLE,FALSE);device->SetRenderState(D3DRS_CLIPPLANEENABLE,0);
    device->SetRenderState(D3DRS_STENCILENABLE,FALSE);device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);device->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);
    device->SetRenderState(D3DRS_COLORWRITEENABLE,15);device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
    device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);
    device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
    device->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);device->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
    device->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);device->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
    if(SUCCEEDED(device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex)))) VRDiagnostics::Increment(textDraws);else VRDiagnostics::Increment(failures);
    for(unsigned i=0;i<4;++i) device->SetRenderTarget(i,targets[i].Get());
    device->SetDepthStencilSurface(depth.Get());state->Apply();device->SetViewport(&viewport);
}

void VRHud::Reset()
{radioSurface.Reset();radioImage.Reset();radioFresh=false;radioAt=0;radarSurface.Reset();radar.Reset();shader.Reset();capturedAt=0;
 overlaySurface.Reset();overlayTexture.Reset();overlayShader.Reset();overlayFresh=false;
 smsSurface.Reset();smsImage.Reset();smsFresh=false;smsAt=0;
 screenSurface.Reset();screenTexture.Reset();screenShared=nullptr;screenWidth=screenHeight=0;}

bool VRHud::WantsScreen(bool loading)
{
    return screens && (loading || (CTimer::ms_bUserPause && *CTimer::ms_bUserPause) ||
        (CTimer::ms_bScriptPause && *CTimer::ms_bScriptPause));
}

bool VRHud::PublishScreen(IDirect3DDevice9* device,bool loading)
{
    if(!WantsScreen(loading)) {OpenXRVR::SetPanelImage(nullptr,nullptr,0,0);return false;}
    if(!device || !OpenXRVR::CanAcceptEyeImages()) return true;
    ComPtr<IDirect3DSurface9> source;
    D3DSURFACE_DESC desc{};
    if(FAILED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&source)) || FAILED(source->GetDesc(&desc))) return true;
    if(desc.Width<640 || desc.Height<480) return false;
    if(!screenTexture)
    {
        if(FAILED(device->CreateTexture(desc.Width,desc.Height+4,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,
            D3DPOOL_DEFAULT,&screenTexture,OpenXRVR::UsesVulkan() ? nullptr:&screenShared)) ||
            FAILED(screenTexture->GetSurfaceLevel(0,&screenSurface))) {VRDiagnostics::Increment(failures);return true;}
        screenWidth=desc.Width;screenHeight=desc.Height;
    }
    if(desc.Width!=screenWidth || desc.Height!=screenHeight) {VRDiagnostics::Increment(failures);return true;}
    const RECT imageRect{0,4,static_cast<LONG>(screenWidth),static_cast<LONG>(screenHeight+4)};
    const RECT colorRect{0,0,static_cast<LONG>(screenWidth),4};
    device->ColorFill(screenSurface.Get(),&colorRect,D3DCOLOR_ARGB(255,64,220,255));
    if(FAILED(device->StretchRect(source.Get(),nullptr,screenSurface.Get(),&imageRect,D3DTEXF_NONE))) {VRDiagnostics::Increment(failures);return true;}
    OpenXRVR::PanelPointer pointer{};
    if(OpenXRVR::GetPanelPointer(pointer) && pointer.active && pointer.hit)
    {
        const LONG x=static_cast<LONG>(pointer.u*(screenWidth-1)),y=4+static_cast<LONG>(pointer.v*(screenHeight-1));
        RECT cursor{std::max(0L,x-5),std::max(4L,y-5),std::min(static_cast<LONG>(screenWidth),x+6),std::min(static_cast<LONG>(screenHeight+4),y+6)};
        device->ColorFill(screenSurface.Get(),&cursor,pointer.down ? D3DCOLOR_ARGB(255,255,255,255):D3DCOLOR_ARGB(255,64,220,255));
    }
    if(!OpenXRVR::UsesVulkan())
    {
        ComPtr<IDirect3DQuery9> fence;
        if(FAILED(device->CreateQuery(D3DQUERYTYPE_EVENT,&fence)) || FAILED(fence->Issue(D3DISSUE_END))) return true;
        const auto deadline=GetTickCount64()+1000;
        HRESULT result;
        do {result=fence->GetData(nullptr,0,D3DGETDATA_FLUSH);if(result==S_FALSE)Sleep(0);}
        while(result==S_FALSE && GetTickCount64()<deadline);
        if(result!=S_OK) {VRDiagnostics::Increment(failures);return true;}
    }
    if(!OpenXRVR::SetPanelImage(screenTexture.Get(),screenShared,screenWidth,screenHeight+4)) VRDiagnostics::Increment(failures);
    return true;
}

std::string VRHud::Status()
{
    char text[1400];sprintf_s(text,"{\"enabled\":%s,\"hookReady\":%s,\"calls\":%u,\"queued\":%u,\"executed\":%u,\"captures\":%u,\"eyeDraws\":%u,\"failures\":%u,\"bounds\":[%.2f,%.2f,%.2f,%.2f],\"panel\":[%.1f,%.1f,%.1f,%.1f],\"wanted\":%u,\"dashboardEnabled\":%s,\"dashboardEyeDraws\":%u,\"headingHookReady\":%s,\"headingOverrides\":%u,\"originalHeading\":%.3f,\"carHeading\":%.3f,\"textHookReady\":%s,\"spriteHookReady\":%s,\"windowHookReady\":%s,\"textCaptures\":%u,\"textEyeDraws\":%u,\"hudWidth\":%.2f,\"smsSlotReady\":%s,\"smsCaptures\":%u,\"smsBounds\":[%.4f,%.4f,%.4f,%.4f]}",
        enabled ? "true":"false",radarDraw ? "true":"false",calls.load(),queued.load(),executed.load(),captures.load(),draws.load(),failures.load(),
        lastBounds[0].load(),lastBounds[1].load(),lastBounds[2].load(),lastBounds[3].load(),lastPanel[0].load(),lastPanel[1].load(),lastPanel[2].load(),lastPanel[3].load(),wanted.load(),dashboard ? "true":"false",dashboardDraws.load(),radarHeadingHook ? "true":"false",radarHeadingOverrides.load(),originalRadarHeading.load(),appliedRadarHeading.load(),fontDraw ? "true":"false",popupSprite ? "true":"false",popupWindow ? "true":"false",textCaptures.load(),textDraws.load(),hudWidth,smsTexture ? "true":"false",smsCaptures.load(),smsBounds[0],smsBounds[1],smsBounds[2],smsBounds[3]);
    std::string result=text;result.pop_back();result+=",\"phoneTargets\":[";
    for(unsigned kind=0;kind<3;++kind) { if(kind) result+=",";result+="[";for(unsigned i=0;i<6;++i) {if(i) result+=",";result+=std::to_string(phoneCaptureTargets[kind][i].load());}result+="]";}
    std::lock_guard guard(vehiclePanelLock);
    return result+"]"+std::format(",\"vehicleModel\":\"{:08X}\",\"vehicleProfile\":\"{}\",\"vehicleProfileSaveOk\":{},\"radioHookReady\":{},\"radioCount\":{},\"radioStation\":{},\"radioCaptures\":{},\"radioEyeDraws\":{},\"radioFresh\":{},\"panelGrips\":[{},{}],\"radioPlayingName\":\"{}\",\"radioRequestedName\":\"{}\",\"radioPendingStation\":{}}}",
        vehicleModel,VehicleProfile(vehicleModel),vehicleProfileSaveOk,radioRender && radioCarDraw && radioCanDraw,
        radioCount.load(),radioStation,radioCaptures.load(),radioDraws.load(),radioAt && GetTickCount64()-radioAt<250,panelGrips[0].panel,panelGrips[1].panel,
        radioPlayingName,radioRequestedName,radioPendingStation);
}

void VRHud::Install()
{
    CIniReader ini("");
    enabled=ini.ReadInteger("VR","WristRadar",0)!=0;
    screens=ini.ReadInteger("VR","WorldSpaceScreens",1)!=0;
    textHud=ini.ReadInteger("VR","ViewHud",1)!=0;
    ApplySettings();
    // CE native vehicle carousel and its resolved CDrawRadioHudTextDC renderer.
    auto carRadio=hook::pattern("83 EC 18 FF 74 24 1C E8 ? ? ? ? 83 C4 04 84 C0 0F 84 ? ? ? ? 8D 44 24 10 6A 06");
    auto canRadio=hook::pattern("A1 ? ? ? ? 8B 4C 24 04 3B 88 40 05 00 00 74 0D 80 3D ? ? ? ? 00");
    auto radioCommand=hook::pattern("83 EC 2C A1 ? ? ? ? 33 C4 89 44 24 28 C7 04 24 00 00 00 00 8B 04 24 89 44 24 08");
    auto radioCountFn=hook::pattern("E8 ? ? ? ? 39 05 ? ? ? ? 0F 42 05 ? ? ? ? C3 CC CC CC");
    auto radioWidthFn=hook::pattern("83 EC 18 A1 ? ? ? ? 48 C7 44 24 08 00 00 00 00 C7 44 24 0C 00 00 00 00 83 F8 03 77 2B");
    auto radioLookupFn=hook::pattern("0F B6 05 ? ? ? ? 8B 4C 24 04 3B C8 73 09 A1 ? ? ? ? 8B 04 88 C3 33 C0 C3");
    auto radioVehicleFn=hook::pattern("E8 ? ? ? ? 85 C0 74 10 F6 80 6C 02 00 00 04 74 07 8B 80 30 0B 00 00 C3 33 C0 C3");
    if(carRadio.size()==1 && canRadio.size()==1 && radioCommand.size()==1 && radioCountFn.size()==1 && radioWidthFn.size()==1 && radioLookupFn.size()==1 && radioVehicleFn.size()==1)
    {
        const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        nativeRadioCount=reinterpret_cast<decltype(nativeRadioCount)>(radioCountFn.get_first());
        nativeRadioByIndex=reinterpret_cast<decltype(nativeRadioByIndex)>(radioLookupFn.get_first());
        nativeRadioWidth=reinterpret_cast<decltype(nativeRadioWidth)>(radioWidthFn.get_first());
        nativeRadioVehicle=reinterpret_cast<decltype(nativeRadioVehicle)>(radioVehicleFn.get_first());
        nativeRadioMode=*reinterpret_cast<unsigned**>(carRadio.get_first<unsigned char>()+88);
        nativeRadioState=*reinterpret_cast<unsigned**>(canRadio.get_first<unsigned char>()+32);
        nativeRadioStation=*reinterpret_cast<unsigned**>(canRadio.get_first<unsigned char>()+46);
        // Shared scroll value is scoped to the producer; resolved commands copy vertices.
        nativeRadioScroll=reinterpret_cast<float*>(base+(0x011A4F0C-0x00400000));
        radioCanDraw=safetyhook::create_inline(canRadio.get_first(),RadioCanDraw);
        radioCarDraw=safetyhook::create_inline(carRadio.get_first(),RadioCarDraw);
        radioRender=safetyhook::create_inline(radioCommand.get_first(),RadioRender);
    }
    // Filter specific native records before font/sprite commands are queued.
    // Keep their fade state advancing, and leave flat-screen rendering native.
    constexpr auto textSlot="6A 05 68 ? ? ? ? E8 ? ? ? ? A3 ? ? ? ?";
    wantedFrontId=FindHudSlot(textSlot,"HUD_WANTED_FRONT",13);
    wantedBackId=FindHudSlot(textSlot,"HUD_WANTED_BACK",13);
    ammoId=FindHudSlot(textSlot,"HUD_AMMO",13);
    weaponIconId=FindHudSlot("6A 02 68 ? ? ? ? E8 ? ? ? ? 8B 3D ? ? ? ? A3","HUD_WEAPON_ICON",19);
    auto fade=hook::pattern("80 7C 24 08 00 53 56 8B 74 24 0C 57 8B 3C B5 ? ? ? ? 0F B6 5F 58");
    auto textRecord=hook::pattern("83 EC 20 56 8B 74 24 28 8B 04 B5 ? ? ? ? 66 83 78 6C 00");
    auto spriteRecord=hook::pattern("55 8B EC 83 E4 F8 83 EC 1C 53 56 57 8B 7D 08 8B 04 BD");
    if(fade.size()==1 && textRecord.size()==1 && spriteRecord.size()==1 && wantedFrontId && wantedBackId && ammoId && weaponIconId)
    {
        advanceHudFade=reinterpret_cast<decltype(advanceHudFade)>(fade.get_first());
        hudText=safetyhook::create_inline(textRecord.get_first(),HudText);
        hudSprite=safetyhook::create_inline(spriteRecord.get_first(),HudSprite);
    }
    auto weaponUpdate=hook::pattern("55 8B EC 83 E4 F8 83 EC 28 53 55 56 57 6A 00 E8 ? ? ? ? 8B E8 83 C4 04 33 DB");
    if(weaponUpdate.size()==1)weaponHudUpdate=safetyhook::create_inline(weaponUpdate.get_first(),WeaponHudUpdate);
    // HUD_TEXT_MESSAGE_ICON initialization: obtain the relocated native texture slot.
    auto sms=hook::pattern("8B 0C 85 ? ? ? ? 83 C4 38 A3 ? ? ? ? 68 ? ? ? ? E8 ? ? ? ? FF 35 ? ? ? ? FF 35 ? ? ? ? 6A 00 68 ? ? ? ? 68 ? ? ? ? 6A 02");
    if(sms.size()==1) smsTexture=*reinterpret_cast<void***>(sms.get_first<unsigned char>()+16);
    if(textHud || enabled)
    {
        auto font=hook::pattern("55 8B EC 83 E4 F8 81 EC C4 00 00 00 A1 ? ? ? ? 33 C4 89 84 24 C0 00 00 00 80 3D ? ? ? ? 00 53 56 57");
        auto sprite=hook::pattern("8B 41 24 83 F8 02 75 1A FF 71 20 8D 41 10 FF 71 1C FF 71 18 50 8D 41 08 50 E8");
        auto window=hook::pattern("FF 71 18 8D 41 10 50 8D 41 08 50 E8 ? ? ? ? 83 C4 0C C3");
        if(font.size()==1) fontDraw=safetyhook::create_inline(font.get_first(),FontDraw);
        if(sprite.size()==1) popupSprite=safetyhook::create_inline(sprite.get_first(),PopupSprite);
        if(window.size()==1) popupWindow=safetyhook::create_inline(window.get_first(),PopupWindow);
        if(!fontDraw) textHud=false;
    }
    if(screens) CSettings::SetVRMenuPointerProvider([](float& u,float& v,bool& select,bool& back,bool& waypoint,uint64_t& stamp){
        OpenXRVR::PanelPointer pointer{};
        if(!OpenXRVR::GetPanelPointer(pointer))return false;
        stamp=pointer.sampledAt;select=pointer.down;back=pointer.back;waypoint=pointer.waypoint;
        u=pointer.hit ? pointer.u:-1.f;v=pointer.hit ? pointer.v:-1.f;
        return pointer.active || pointer.back;
    });
    FusionFix::onBeforeReset()+=[](){Reset();};
    if(enabled) FusionFix::onGameProcessEvent() += [](){
        SaveVehicleLayouts();
        vehicleRadioAvailable=Allowed() && VehicleHasRadio();
        OpenXRVR::ControllerInput input{};
        if(OpenXRVR::GetControllerInput(input))
        {
            const bool next=input.radioPresses[0]!=stationPresses[0],previous=input.radioPresses[1]!=stationPresses[1];
            stationPresses[0]=input.radioPresses[0];stationPresses[1]=input.radioPresses[1];
            if(input.driving && Allowed() && vehicleRadioAvailable && !input.phoneActive)
            {
                using H=Natives::NativeHashes;
                if(next && NativeInvoke::GetNativeHandler(std::to_underlying(H::RETUNE_RADIO_UP)))Natives::RetuneRadioUp();
                if(previous && NativeInvoke::GetNativeHandler(std::to_underlying(H::RETUNE_RADIO_DOWN)))Natives::RetuneRadioDown();
                const int station=requestedStation.exchange(-1);
                if(station>=0 && unsigned(station)<radioCount && NativeInvoke::GetNativeHandler(std::to_underlying(H::RETUNE_RADIO_TO_STATION_NAME)))
                {
                    const auto* name=NativeRadioName(unsigned(station));
                    if(name && *name)
                    {
                        const std::string requested=name;
                        Natives::RetuneRadioToStationName(const_cast<char*>(requested.c_str()));
                        std::lock_guard guard(vehiclePanelLock);
                        radioRequestedName=requested;radioPendingStation=station;radioRequestedAt=GetTickCount64();
                    }
                }
                if(NativeInvoke::GetNativeHandler(std::to_underlying(H::GET_PLAYER_RADIO_STATION_NAME)))
                {
                    const auto* name=Natives::GetPlayerRadioStationName();
                    const std::string playing=name && *name ? name:"OFF";
                    const unsigned count=radioCount;int current=-1;
                    for(unsigned i=0;i<count;++i)
                        if(const auto* candidate=NativeRadioName(i);candidate && _stricmp(candidate,playing.c_str())==0)
                        {current=int(i);break;}
                    std::lock_guard guard(vehiclePanelLock);radioPlayingName=playing;
                    // The script index getter aliases several native entries to
                    // 16. Reconcile by identity, and allow native retuning to finish.
                    if(radioPendingStation>=0 && (current==radioPendingStation || GetTickCount64()-radioRequestedAt>3000))radioPendingStation=-1;
                    if(!radioDragging && radioPendingStation<0 && current>=0)
                    {radioStation=unsigned(current);radioCursor=float(radioStation);}
                }
            }
            else requestedStation=-1;
        }
        carRadarAt=0;
        if(dashboard && Allowed())
        {
            using Hash=Natives::NativeHashes;
            bool handlers=true;
            for(auto hash:{Hash::GET_PLAYER_ID,Hash::GET_PLAYER_CHAR,Hash::DOES_CHAR_EXIST,
                Hash::IS_CHAR_SITTING_IN_ANY_CAR,Hash::GET_CAR_CHAR_IS_USING,Hash::DOES_VEHICLE_EXIST,
                Hash::GET_CAR_MODEL,Hash::GET_CAR_HEADING})
                handlers &= NativeInvoke::GetNativeHandler(std::to_underlying(hash))!=nullptr;
            if(handlers)
            {
                Ped ped=0;Natives::GetPlayerChar(Natives::GetPlayerId(),&ped);
                if(ped && Natives::DoesCharExist(ped) && (Natives::IsCharSittingInAnyCar(ped) || Natives::IsCharInAnyTrain(ped)))
                {
                    Vehicle car=0;Natives::GetCarCharIsUsing(ped,&car);
                    if(car && Natives::DoesVehicleExist(car))
                    {
                        uint32_t model=0;Natives::GetCarModel(car,&model);
                        if(model)
                        {
                            float heading=NAN;Natives::GetCarHeading(car,&heading);
                            if(std::isfinite(heading))
                            {carRadarHeading=heading*.01745329252f;carRadarAt=GetTickCount64();}
                        }
                    }
                }
            }
        }
        if(!NativeInvoke::GetNativeHandler(std::to_underlying(Natives::NativeHashes::STORE_WANTED_LEVEL)) ||
            !NativeInvoke::GetNativeHandler(std::to_underlying(Natives::NativeHashes::WANTED_STARS_ARE_FLASHING))) return;
        uint32_t level=0;Natives::StoreWantedLevel(0,&level);
        wanted=std::min(level,6u);wantedFlashing=Natives::WantedStarsAreFlashing();
    };
    dashboard=ini.ReadInteger("VR","DashboardRadar",1)!=0;
    if(!enabled) return;
    auto signature=hook::pattern("83 EC 10 53 6A 00 E8 ? ? ? ? A1 ? ? ? ? 33 C9 85 C0 0F 48 C8 51 6A 01");
    if(signature.size()==1) radarDraw=safetyhook::create_inline(signature.get_first(),RadarDraw);
    if(!radarDraw) enabled=false;
    // CE radar update 005DBE33: camera heading -> radar angle 0118F4B4.
    // Override at its store, before player/blip update, without altering the camera.
    auto headingStore=hook::pattern("F3 0F 11 05 ? ? ? ? E8 ? ? ? ? 6A 00 E8 ? ? ? ? 83 C4 04 83 F8 01");
    if(headingStore.size()==1) radarHeadingHook=safetyhook::create_mid(headingStore.get_first(),[](SafetyHookContext& regs){
        const auto stamp=carRadarAt.load();
        if(!Allowed() || !dashboard || !stamp || GetTickCount64()-stamp>250)return;
        {if(VRDiagnostics::counters) originalRadarHeading=regs.xmm0.f32[0];}
        regs.xmm0.f32[0]=carRadarHeading.load();
        {if(VRDiagnostics::counters) appliedRadarHeading=regs.xmm0.f32[0];}VRDiagnostics::Increment(radarHeadingOverrides);
    });
}
