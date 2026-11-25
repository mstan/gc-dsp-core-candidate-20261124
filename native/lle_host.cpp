// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted Dolphin DSP Host: real owner memory callbacks and caller-owned ARAM.
#include "engine_internal.hpp"
#include "Core/DSP/DSPHost.h"
#include "Core/DSP/DSPAnalyzer.h"
#include "Core/DSP/Aot/Aot.h"
#include "Common/Hash.h"
#include <stdexcept>

namespace {
GcnDspEngine& owner() {
    if(!gcn_dsp_owner)throw std::runtime_error("DSP host has no owner");
    return *gcn_dsp_owner;
}
uint32_t checked_offset(uint32_t address,uint32_t bytes) {
    auto& engine=owner();const uint32_t offset=address&0x1fffffffu;const auto& c=engine.config;
    if(c.wii && offset>=0x10000000u && offset-0x10000000u<=c.mem2_size &&
       bytes<=c.mem2_size-(offset-0x10000000u))return offset;
    if(offset>c.mem1_size || bytes>c.mem1_size-offset)
        throw std::runtime_error("DSP MEM1 transfer outside its contract");
    return offset;
}
uint8_t* pointer(uint32_t offset) {
    const auto& c=owner().config;
    return c.wii && offset>=0x10000000u ? c.mem2+(offset-0x10000000u):c.mem1+offset;
}
void observe(uint32_t offset,uint32_t bytes,bool writing) {
    const auto& c=owner().config;
    if(bytes && (!c.prepare(c.user,offset,bytes,writing) || (writing && !c.written(c.user,offset,bytes))))
        throw std::runtime_error("DSP MEM1 observation/write rejected");
}
uint32_t span(uint32_t address,uint32_t bytes,bool writing) {
    const auto offset=checked_offset(address,bytes);observe(offset,bytes,writing);
    return offset;
}
}
namespace DSP::Host {
u8 ReadHostMemory(u32 address) {
    if(!owner().config.wii)return owner().config.aram[address&0xffffffu];
    return *pointer(span(address,1,false));
}
void WriteHostMemory(u8 value,u32 address) {
    if(!owner().config.wii) {owner().config.aram[address&0xffffffu]=value;return;}
    *pointer(span(address,1,true))=value;
}
void DMAToDSP(u16* destination,u32 address,u32 bytes) {
    // Match the donor's CopyFromEmuSwapped<u16>: validate the requested span,
    // then transfer complete words only. Zelda legitimately requests227 bytes.
    const auto at=checked_offset(address,bytes);const auto count=bytes&~1u;
    observe(at,count,false);const auto* mem=pointer(at);
    for(u32 i=0;i<count;i+=2)destination[i/2]=u16(mem[i])<<8|mem[i+1];
}
void DMAFromDSP(const u16* source,u32 address,u32 bytes) {
    const auto at=checked_offset(address,bytes);const auto count=bytes&~1u;
    observe(at,count,true);auto* mem=pointer(at);
    for(u32 i=0;i<count;i+=2) {mem[i]=u8(source[i/2]>>8);mem[i+1]=u8(source[i/2]);}
}
void OSD_AddMessage(std::string,u32) {}
bool OnThread() {return false;}
bool IsWiiHost() {return owner().config.wii;}
void InterruptRequest() {const auto& c=owner().config;c.interrupt(c.user);}
void CodeLoaded(DSPCore& dsp,u32 address,size_t bytes) {
    if(bytes>UINT32_MAX)throw std::runtime_error("DSP image size overflow");
    const auto at=span(address,uint32_t(bytes),false);
    CodeLoaded(dsp,pointer(at),bytes);
}
void CodeLoaded(DSPCore& dsp,const u8* data,size_t bytes) {
    auto& state=dsp.DSPState();
    if(data)state.SetIRAMCRC(Common::HashAdler32(data,bytes));
    dsp.ClearIRAM();Aot::CaptureCode(state,0);state.GetAnalyzer().Analyze(state);
}
}
