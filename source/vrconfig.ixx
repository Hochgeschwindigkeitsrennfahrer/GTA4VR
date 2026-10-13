module;
#include <common.hxx>
#include "includes/vrsettingsmodel.hpp"
#include <atomic>
export module vrconfig;

export namespace VRConfig
{
    using Number=VRSettingsModel::Number;
    using Flag=VRSettingsModel::Flag;
    using Values=VRSettingsModel::Values;
    void Initialize();
    unsigned Revision();
    Values Read();
    bool Set(Number id,float value);
    bool Set(Flag id,bool value);
}
namespace
{
    SRWLOCK configLock=SRWLOCK_INIT;
    VRConfig::Values configured;
    std::atomic<unsigned> revision{0};
}
void VRConfig::Initialize()
{
    CIniReader ini("");
    AcquireSRWLockExclusive(&configLock);
    for(unsigned i=0;i<configured.values.size();++i)
    {
        const auto& spec=VRSettingsModel::numbers[i];
        const float value=ini.ReadFloat("VR",spec.key,spec.fallback);
        configured.values[i]=VRSettingsModel::Valid(Number(i),value) ? value:spec.fallback;
    }
    for(unsigned i=0;i<configured.enabled.size();++i)
        configured.enabled[i]=ini.ReadInteger("VR",VRSettingsModel::flags[i],configured.enabled[i])!=0;
    revision.fetch_add(1,std::memory_order_release);
    ReleaseSRWLockExclusive(&configLock);
}
unsigned VRConfig::Revision() {return revision.load(std::memory_order_acquire);}
VRConfig::Values VRConfig::Read()
{
    AcquireSRWLockShared(&configLock);auto result=configured;ReleaseSRWLockShared(&configLock);return result;
}
bool VRConfig::Set(Number id,float value)
{
    if(!VRSettingsModel::Valid(id,value))return false;
    AcquireSRWLockExclusive(&configLock);
    try
    {
        const auto key=VRSettingsModel::numbers[unsigned(id)].key;
        CIniReader ini("");ini.WriteString("VR",key,std::format("{:.9g}",value),true);
        if(CIniReader("").ReadFloat("VR",key,NAN)!=value)
        {ReleaseSRWLockExclusive(&configLock);return false;}
        configured.values[unsigned(id)]=value;revision.fetch_add(1,std::memory_order_release);
    }
    catch(...) {ReleaseSRWLockExclusive(&configLock);return false;}
    ReleaseSRWLockExclusive(&configLock);return true;
}
bool VRConfig::Set(Flag id,bool value)
{
    if(unsigned(id)>=VRSettingsModel::flags.size())return false;
    AcquireSRWLockExclusive(&configLock);
    try
    {
        const auto key=VRSettingsModel::flags[unsigned(id)];
        CIniReader ini("");ini.WriteInteger("VR",key,value ? 1:0,true);
        if(CIniReader("").ReadInteger("VR",key,-1)!=(value ? 1:0))
        {ReleaseSRWLockExclusive(&configLock);return false;}
        configured.enabled[unsigned(id)]=value;revision.fetch_add(1,std::memory_order_release);
    }
    catch(...) {ReleaseSRWLockExclusive(&configLock);return false;}
    ReleaseSRWLockExclusive(&configLock);return true;
}
