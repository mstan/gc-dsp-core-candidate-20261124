// SPDX-License-Identifier: GPL-3.0-or-later
// Existing BlueWake/Dolphin HLE behind the same build-selected engine contract.
#include "gcnllr/native_dsp.h"
#include "dsp_adapter_c.h"
#include <algorithm>
#include <limits>
#include <new>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
struct GcnDspEngine {
    GcnDspEngineConfig config{};
    BluewakeDspAdapter* dsp{};
    uint16_t mail_high{};
    bool mail_high_written{},failed{};
};
static GcnDspEngine* owner;
static bool ready(const GcnDspEngine* e) {return e && e==owner && e->dsp && !e->failed;}
template<class F> static bool checked(GcnDspEngine* e,F&& fn) {
    if(!ready(e))return false;
    try {fn();return !e->failed;}
    catch(const std::exception& error) {
        e->failed=true;
        std::fprintf(stderr,"[native-dsp-hle] %s\n",error.what());
        return false;
    }
}
static uint8_t* pointer(void* user,uint32_t address,uint32_t size) {
    auto& e=*static_cast<GcnDspEngine*>(user);const auto& c=e.config;
    const uint32_t offset=address&0x1fffffffu;
    // The donor lends the same pointer for both directions. Conservatively
    // preserve its bytes and announce possible writes before returning it.
    if(offset>c.mem1_size || size>c.mem1_size-offset ||
       !c.prepare(c.user,offset,size,true) || !c.written(c.user,offset,size)) {
        e.failed=true;
        // ROM/mixer helpers can assume a non-null pointer. Stop within their
        // C++ stack; checked() catches before returning through our C ABI.
        throw std::runtime_error("DSP MEM1 observation/write rejected");
    }
    return c.mem1+offset;
}
static uint64_t timebase(void* user) {
    const auto& c=static_cast<GcnDspEngine*>(user)->config;return c.timebase(c.user);
}
static void interrupt(void* user) {
    const auto& c=static_cast<GcnDspEngine*>(user)->config;c.interrupt(c.user);
}
extern "C" GcnDspEngine* gcn_dsp_engine_create(const GcnDspEngineConfig* c) {
    if(!c || c->wii || owner || !c->mem1 || !c->mem1_size || !c->aram || c->aram_size!=0x1000000u ||
       !c->prepare || !c->written || !c->timebase || !c->interrupt)return nullptr;
    auto* e=new(std::nothrow) GcnDspEngine;if(!e)return nullptr;e->config=*c;
    try {
        e->dsp=bluewake_dsp_adapter_create_hle(e,pointer,c->aram,c->aram_size,timebase,interrupt);
        if(!e->dsp || !bluewake_dsp_adapter_is_hle(e->dsp) || e->failed)
            throw std::runtime_error("DSP HLE initialization failed");
        owner=e;return e;
    }catch(const std::exception& error) {
        std::fprintf(stderr,"[native-dsp-hle] initialization: %s\n",error.what());
        bluewake_dsp_adapter_destroy(e->dsp);delete e;return nullptr;
    }
}
extern "C" void gcn_dsp_engine_destroy(GcnDspEngine* e) {
    if(!e || e!=owner)return;
    bluewake_dsp_adapter_destroy(e->dsp);owner=nullptr;delete e;
}
extern "C" bool gcn_dsp_engine_update_pending(const GcnDspEngine* e) {
    return !ready(e) || bluewake_dsp_adapter_update_pending(e->dsp);
}
extern "C" bool gcn_dsp_engine_run(GcnDspEngine* e,uint32_t cycles) {
    return checked(e,[&]{
        while(cycles) {
            const auto slice=std::min<uint32_t>(cycles,std::numeric_limits<int>::max());
            bluewake_dsp_adapter_run_cycles(e->dsp,int(slice));
            if(e->failed)throw std::runtime_error("DSP HLE update failed");
            cycles-=slice;
        }
    });
}
extern "C" bool gcn_dsp_engine_read_control(GcnDspEngine* e,uint16_t* out) {
    return out && checked(e,[&]{*out=bluewake_dsp_adapter_read_control(e->dsp);});
}
extern "C" bool gcn_dsp_engine_write_control(GcnDspEngine* e,uint16_t value) {
    return checked(e,[&]{bluewake_dsp_adapter_write_control(e->dsp,value);});
}
extern "C" bool gcn_dsp_engine_read_mail(GcnDspEngine* e,bool cpu,bool low,uint16_t* out) {
    return out && checked(e,[&]{
        const auto value=cpu ? bluewake_dsp_adapter_peek_cpu_mailbox(e->dsp):bluewake_dsp_adapter_peek_dsp_mailbox(e->dsp);
        if(low)*out=cpu ? uint16_t(value):bluewake_dsp_adapter_read_dsp_mailbox_low(e->dsp);
        else *out=cpu && e->mail_high_written ? uint16_t((e->mail_high&0x7fffu)|(value>>16&0x8000u)):uint16_t(value>>16);
    });
}
extern "C" bool gcn_dsp_engine_write_mail(GcnDspEngine* e,bool low,uint16_t value) {
    return checked(e,[&]{
        // The donor consumes protocol payloads, not a hardware mailbox image.
        // Preserve the CPU's raw high half: setting a synthetic busy bit turns
        // Zelda's zero sync into a different command. ROM's status-bearing
        // echo is adapted separately at that service boundary.
        if(low)bluewake_dsp_adapter_write_cpu_mailbox(e->dsp,uint32_t(e->mail_high)<<16|value);
        else {e->mail_high=value;e->mail_high_written=true;}
    });
}
extern "C" uint16_t gcn_dsp_engine_peek_mail_high(const GcnDspEngine* e,bool cpu) {
    if(!e || e!=owner || !e->dsp)return 0;
    const auto value=cpu ? bluewake_dsp_adapter_peek_cpu_mailbox(e->dsp):bluewake_dsp_adapter_peek_dsp_mailbox(e->dsp);
    return cpu && e->mail_high_written ? uint16_t((e->mail_high&0x7fffu)|(value>>16&0x8000u)):uint16_t(value>>16);
}
extern "C" GcnDspEngineStats gcn_dsp_engine_stats(const GcnDspEngine* e) {
    GcnDspEngineStats s{};s.provider=1;s.failed=!ready(e);
    // HLE replaces DSP execution; native-instruction retirement is not a
    // meaningful HLE counter. Do not relabel native host calls as guest DSP.
    if(e && e==owner && e->dsp)s.control=bluewake_dsp_adapter_read_control(e->dsp);
    return s;
}

namespace {
struct HleSnapshot {
    uint32_t payload_size;
    uint16_t mail_high;
    uint16_t mail_high_written;
};
using StateBytes=std::unique_ptr<uint8_t,decltype(&std::free)>;
}

extern "C" bool gcn_dsp_engine_capture(GcnDspEngine* engine,uint8_t** bytes,size_t* size) {
    if(!bytes || !size)return false;
    *bytes=nullptr;*size=0;
    if(!ready(engine))return false;
    try {
        uint8_t* raw=nullptr;
        const size_t payload_size=bluewake_dsp_adapter_save_state(engine->dsp,&raw);
        const StateBytes payload(raw,std::free);
        if(!payload_size || payload_size>UINT32_MAX || payload_size>SIZE_MAX-sizeof(HleSnapshot))return false;
        const HleSnapshot header{uint32_t(payload_size),engine->mail_high,uint16_t(engine->mail_high_written)};
        auto* result=static_cast<uint8_t*>(std::malloc(sizeof(header)+payload_size));
        if(!result)return false;
        std::memcpy(result,&header,sizeof(header));
        std::memcpy(result+sizeof(header),payload.get(),payload_size);
        *bytes=result;*size=sizeof(header)+payload_size;
        return true;
    }catch(const std::exception&) {return false;}
}

extern "C" bool gcn_dsp_engine_restore(GcnDspEngine* engine,const uint8_t* bytes,size_t size) {
    if(!ready(engine) || !bytes || size<sizeof(HleSnapshot))return false;
    HleSnapshot header;
    std::memcpy(&header,bytes,sizeof(header));
    if(!header.payload_size || header.payload_size!=size-sizeof(header) || header.mail_high_written>1)return false;
    uint8_t* raw=nullptr;
    size_t original_size=0;
    try {original_size=bluewake_dsp_adapter_save_state(engine->dsp,&raw);}
    catch(const std::exception&) {return false;}
    const StateBytes original(raw,std::free);
    if(!original_size)return false;
    try {
        if(bluewake_dsp_adapter_load_state(engine->dsp,bytes+sizeof(header),header.payload_size)) {
            engine->mail_high=header.mail_high;
            engine->mail_high_written=header.mail_high_written!=0;
            return true;
        }
    }catch(const std::exception&) {}
    try {engine->failed=!bluewake_dsp_adapter_load_state(engine->dsp,original.get(),original_size);}
    catch(const std::exception&) {engine->failed=true;}
    return false;
}
