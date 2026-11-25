// SPDX-License-Identifier: GPL-3.0-or-later
// Device-independent port of vendored Dolphin dsp_lle_c.cpp. Build-selected
// engine; no donor CPU, synthetic boot state, poll clock or process exit.
#include "engine_internal.hpp"
#include "src/dsp_core_state.hpp"
#include "Core/DSP/DSPAccelerator.h"
#include "Core/DSP/DSPTables.h"
#include "Core/DSP/Interpreter/DSPInterpreter.h"
#include "Core/DSP/Interpreter/DSPIntTables.h"
#include "Core/DSP/Aot/Aot.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

GcnDspEngine* gcn_dsp_owner{};
void gcn_dsp_engine_check(const GcnDspEngine& engine) {
    if(engine.failed || !engine.core)throw std::runtime_error("DSP engine unavailable");
    if(const auto* aot=engine.core->GetAot();aot && aot->GetStats().failed)
        throw std::runtime_error("Static DSP coverage miss; interpreter disabled");
}
template<class F> static bool checked(GcnDspEngine* engine,F&& fn) {
    if(!engine || engine!=gcn_dsp_owner || engine->failed)return false;
    try {
        gcn_dsp_engine_check(*engine);fn();gcn_dsp_engine_check(*engine);return true;
    }catch(const std::exception& e) {
        engine->failed=true;std::fprintf(stderr,"[native-dsp-lle] %s\n",e.what());return false;
    }
}
extern "C" uint64_t gcn_dsp_engine_timebase_v1(void) {
    return gcn_dsp_owner ? gcn_dsp_owner->config.timebase(gcn_dsp_owner->config.user):0;
}
extern "C" GcnDspEngine* gcn_dsp_engine_create(const GcnDspEngineConfig* config) {
    if(!config || gcn_dsp_owner || !config->mem1 || !config->mem1_size ||
       !config->aram || config->aram_size!=0x1000000u ||
       !config->irom || config->irom_size!=8192u || !config->coef || config->coef_size!=4096u ||
       !config->prepare || !config->written || !config->timebase || !config->interrupt ||
       (config->wii && (!config->mem2 || config->mem2_size!=0x4000000u)))return nullptr;
    std::unique_ptr<GcnDspEngine> engine(new(std::nothrow) GcnDspEngine);
    if(!engine)return nullptr;
    engine->config=*config;gcn_dsp_owner=engine.get();
    try {
        engine->core=std::make_unique<DSP::DSPCore>();
        DSP::DSPInitOptions opts;
#if GCN_DSP_ENGINE_AOT
        opts.core_type=DSP::DSPInitOptions::CoreType::AOT;
#else
        opts.core_type=DSP::DSPInitOptions::CoreType::Interpreter;
#endif
        for(uint32_t i=0;i<DSP::DSP_IROM_SIZE;i++)opts.irom_contents[i]=uint16_t(config->irom[2*i])<<8|config->irom[2*i+1];
        for(uint32_t i=0;i<DSP::DSP_COEF_SIZE;i++)opts.coef_contents[i]=uint16_t(config->coef[2*i])<<8|config->coef[2*i+1];
        if(!engine->core->Initialize(opts))throw std::runtime_error("DSP initialization failed");
        DSP::Aot::CaptureCode(engine->core->DSPState(),0x8000);
        engine->core->Reset();DSP::InitInstructionTable();DSP::Interpreter::FinalizeInstructionTables();
        engine->core->SetState(DSP::State::Running);gcn_dsp_engine_check(*engine);
#if GCN_DSP_ENGINE_AOT
        if(!engine->core->GetAot())throw std::runtime_error("DSP AOT engine missing");
#endif
        return engine.release();
    }catch(const std::exception& e) {
        std::fprintf(stderr,"[native-dsp-lle] initialization: %s\n",e.what());
        engine.reset();gcn_dsp_owner=nullptr;return nullptr;
    }
}
extern "C" void gcn_dsp_engine_destroy(GcnDspEngine* engine) {
    if(!engine || engine!=gcn_dsp_owner)return;
    if(engine->core && engine->core->GetAot())engine->core->GetAot()->Report();
    engine->core.reset();gcn_dsp_owner=nullptr;delete engine;
}
extern "C" bool gcn_dsp_engine_update_pending(const GcnDspEngine*) {
    return true;
}
extern "C" bool gcn_dsp_engine_run(GcnDspEngine* engine,uint32_t cycles) {
    return checked(engine,[&]{
        while(cycles) {
            const auto slice=std::min<uint32_t>(cycles,std::numeric_limits<int>::max());
            engine->core->RunCycles(int(slice));gcn_dsp_engine_check(*engine);cycles-=slice;
        }
    });
}
extern "C" bool gcn_dsp_engine_read_control(GcnDspEngine* engine,uint16_t* out) {
    return out && checked(engine,[&]{*out=engine->core->GetInterpreter().ReadControlRegister();});
}
extern "C" bool gcn_dsp_engine_write_control(GcnDspEngine* engine,uint16_t value) {
    return checked(engine,[&]{
        engine->core->GetInterpreter().WriteControlRegister(value);
        if(value&DSP::CR_EXTERNAL_INT) {engine->core->CheckExternalInterrupt();engine->core->CheckExceptions();}
    });
}
extern "C" bool gcn_dsp_engine_read_mail(GcnDspEngine* engine,bool cpu,bool low,uint16_t* out) {
    return out && checked(engine,[&]{
        const auto box=cpu ? DSP::Mailbox::CPU:DSP::Mailbox::DSP;
        *out=low ? engine->core->ReadMailboxLow(box):engine->core->ReadMailboxHigh(box);
    });
}
extern "C" bool gcn_dsp_engine_write_mail(GcnDspEngine* engine,bool low,uint16_t value) {
    return checked(engine,[&]{
        if(low)engine->core->WriteMailboxLow(DSP::Mailbox::CPU,value);
        else engine->core->WriteMailboxHigh(DSP::Mailbox::CPU,value);
    });
}
extern "C" uint16_t gcn_dsp_engine_peek_mail_high(const GcnDspEngine* engine,bool cpu) {
    if(!engine || engine!=gcn_dsp_owner || !engine->core)return 0;
    return uint16_t(engine->core->PeekMailbox(cpu ? DSP::Mailbox::CPU:DSP::Mailbox::DSP)>>16);
}
extern "C" GcnDspEngineStats gcn_dsp_engine_stats(const GcnDspEngine* engine) {
    GcnDspEngineStats result{};
    if(!engine || engine!=gcn_dsp_owner || !engine->core) {result.failed=1;return result;}
    result.pc=engine->core->DSPState().pc;result.control=engine->core->DSPState().control_reg;
    result.interpreter_instructions=engine->core->GetInterpreter().InterpretedInstructions();
    if(const auto* aot=engine->core->GetAot()) {
        const auto& stats=aot->GetStats();result.provider=2;result.failed=stats.failed;
        result.native_instructions=stats.native;result.misses=stats.misses;result.bindings=stats.bindings;
    }else result.provider=3;
    result.failed|=engine->failed;return result;
}

extern "C" bool gcn_dsp_engine_capture(GcnDspEngine* engine,uint8_t** bytes,size_t* size) {
    if(!bytes || !size)return false;
    *bytes=nullptr;*size=0;
    auto* snapshot=static_cast<GcnDspLleSnapshot*>(std::malloc(sizeof(GcnDspLleSnapshot)));
    if(!snapshot)return false;
    bool captured=false;
    if(!checked(engine,[&]{captured=gcn_dsp_core_capture(engine->core.get(),snapshot,0);}) || !captured) {
        std::free(snapshot);
        return false;
    }
    *bytes=reinterpret_cast<uint8_t*>(snapshot);
    *size=sizeof(*snapshot);
    return true;
}

extern "C" bool gcn_dsp_engine_restore(GcnDspEngine* engine,const uint8_t* bytes,size_t size) {
    if(!bytes || size!=sizeof(GcnDspLleSnapshot))return false;
    GcnDspLleSnapshot snapshot;
    std::memcpy(&snapshot,bytes,sizeof(snapshot));
    bool restored=false;
    return checked(engine,[&]{restored=gcn_dsp_core_restore(engine->core.get(),&snapshot);}) && restored;
}
