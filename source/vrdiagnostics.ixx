module;
#include <common.hxx>
#include <atomic>
export module vrdiagnostics;
import common;

// Read once before installing VR hooks. Never poll the INI in hot callbacks.
export namespace VRDiagnostics
{
    inline bool counters=false, logging=false, observationHooks=false, automaticCaptures=false;
    void Install()
    {
        CIniReader ini("");
        counters=ini.ReadInteger("DEVELOPMENT","DiagnosticCounters",0)!=0;
        logging=ini.ReadInteger("DEVELOPMENT","DiagnosticLogging",0)!=0;
        observationHooks=ini.ReadInteger("DEVELOPMENT","DiagnosticObservationHooks",0)!=0;
        automaticCaptures=ini.ReadInteger("DEVELOPMENT","DiagnosticAutomaticCaptures",0)!=0;
    }
    template<class T> auto Increment(T& value)
    {
        if(counters) return ++value;
        if constexpr(requires {value.load();}) return decltype(value.load()){};
        else return T{};
    }
}
