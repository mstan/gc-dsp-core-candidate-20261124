// SPDX-License-Identifier: MIT
#include "gcnllr/native_dsp.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
struct Fixture {
    std::vector<uint8_t> ram=std::vector<uint8_t>(24*1024*1024);
    std::vector<uint8_t> aram=std::vector<uint8_t>(16*1024*1024);
    uint64_t now{};unsigned irqs{};bool reject{};
};
static bool prepare(void* u,uint32_t a,uint32_t n,bool) {auto& f=*static_cast<Fixture*>(u);return !f.reject && a<=f.ram.size() && n<=f.ram.size()-a;}
static bool written(void* u,uint32_t a,uint32_t n) {return prepare(u,a,n,true);}
static uint64_t ticks(void* u) {return static_cast<Fixture*>(u)->now;}
static void irq(void* u) {++static_cast<Fixture*>(u)->irqs;}
static std::vector<uint8_t> read(const char* path,size_t n) {
    std::ifstream s(path,std::ios::binary);assert(s);std::vector<uint8_t> v((std::istreambuf_iterator<char>(s)),{});assert(v.size()==n);return v;
}
static uint32_t consume(GcnDspEngine* e) {
    uint16_t h{},h2{},l{};
    assert(gcn_dsp_engine_read_mail(e,false,false,&h) && (h&0x8000));
    assert(gcn_dsp_engine_peek_mail_high(e,false)==h);
    assert(gcn_dsp_engine_read_mail(e,false,false,&h2) && h==h2);
    assert(gcn_dsp_engine_read_mail(e,false,true,&l));
    return uint32_t(h)<<16|l;
}
int main(int argc,char** argv) {
    (void)argc;(void)argv;std::vector<uint8_t> rom(8192),coef(4096);Fixture f;
    GcnDspEngineConfig c{f.ram.data(),uint32_t(f.ram.size()),f.aram.data(),uint32_t(f.aram.size()),
        rom.data(),uint32_t(rom.size()),coef.data(),uint32_t(coef.size()),&f,prepare,written,ticks,irq};
    auto* e=gcn_dsp_engine_create(&c);assert(e && !gcn_dsp_engine_create(&c));
    assert(gcn_dsp_engine_write_control(e,0x800) && gcn_dsp_engine_run(e,10000));
    const auto boot=consume(e);printf("boot=%08x\n",boot);assert(boot==0x8071feed);
    uint16_t h{};assert(gcn_dsp_engine_read_mail(e,false,false,&h) && !(h&0x8000));
    // ROM echo is a caller-visible service exercised by both actual IROM and
    // the donor ROM HLE. A partially written CPU high half is not published.
    assert(gcn_dsp_engine_write_mail(e,false,0x9234));
    assert(gcn_dsp_engine_read_mail(e,true,false,&h) && h==0x1234);
    assert(gcn_dsp_engine_run(e,10000));
    assert(gcn_dsp_engine_read_mail(e,false,false,&h) && !(h&0x8000));
    assert(gcn_dsp_engine_write_mail(e,true,0x5678));
    assert(gcn_dsp_engine_run(e,10000));
    const auto echo=consume(e);printf("echo=%08x\n",echo);
    if(echo!=0xfeee9234) {std::fflush(stdout);gcn_dsp_engine_destroy(e);return 1;}
    assert(gcn_dsp_engine_read_mail(e,true,false,&h) && !(h&0x8000));
    const auto s=gcn_dsp_engine_stats(e);
    assert(!s.failed && !s.misses);
#ifdef DSP_DEVELOPMENT_ORACLE
    assert(s.provider==3 && s.interpreter_instructions>14);
#else
    assert(!s.interpreter_instructions);
    if(s.provider==2)assert(s.native_instructions>14 && s.bindings>0);
    else assert(s.provider==1 && !s.native_instructions);
#endif
    printf("mail contract provider=%u native=%llu interpreter=%llu misses=%llu\n",s.provider,
        (unsigned long long)s.native_instructions,(unsigned long long)s.interpreter_instructions,(unsigned long long)s.misses);
    uint8_t* snapshot=nullptr;size_t snapshot_size=0;
    assert(gcn_dsp_engine_capture(e,&snapshot,&snapshot_size));
    assert(snapshot && snapshot_size && gcn_dsp_engine_restore(e,snapshot,snapshot_size));
    assert(!gcn_dsp_engine_restore(e,snapshot,snapshot_size-1));
    std::free(snapshot);
    // Both ROM providers must stop when the machine rejects the microcode
    // source span; the HLE hasher must never dereference a null callback result.
    // Valid command messages are published by low writes, including bit 31.
    const uint32_t upload[]={0x80f3a001,0x80002000,0x80f3a002,32,
        0x80f3b002,0,0x80f3c002,0,0x80f3d001,0};
    f.reject=true;
    bool rejected=false;
    for(auto message:upload) {
        if(!gcn_dsp_engine_write_mail(e,false,uint16_t(message>>16)) ||
           !gcn_dsp_engine_write_mail(e,true,uint16_t(message)) ||
           !gcn_dsp_engine_run(e,10000)) {rejected=true;break;}
        // A donor idle skip can defer a ready mailbox to its next scheduling
        // slice. Wait for consumption before publishing the next parameter.
        unsigned polls=0;
        while(gcn_dsp_engine_peek_mail_high(e,true)&0x8000u) {
            assert(++polls<128);
            if(!gcn_dsp_engine_run(e,10000)) {rejected=true;break;}
        }
        if(rejected)break;
    }
    const auto failed=gcn_dsp_engine_stats(e);
    std::printf("upload failure=%d pc=%04x native=%llu from=%04x\n",int(rejected),failed.pc,(unsigned long long)failed.native_instructions,gcn_dsp_engine_peek_mail_high(e,false));std::fflush(stdout);
    assert(rejected && failed.failed && !failed.misses);
#ifndef DSP_DEVELOPMENT_ORACLE
    assert(!failed.interpreter_instructions);
#endif
    assert(!gcn_dsp_engine_run(e,1));
    gcn_dsp_engine_destroy(e);f.reject=false;
    e=gcn_dsp_engine_create(&c);assert(e);gcn_dsp_engine_destroy(e);
    // Each invalid ROM command starts from the reset protocol state.
    // Publication is identical for raw high1234 and9234: busy supplies bit15.
    for(uint16_t high : {uint16_t(0x1234),uint16_t(0)}) {
        e=gcn_dsp_engine_create(&c);assert(e);
        assert(gcn_dsp_engine_write_control(e,0x800) && gcn_dsp_engine_run(e,10000));
        assert(consume(e)==0x8071feed);
        assert(gcn_dsp_engine_write_mail(e,false,high));
        assert(gcn_dsp_engine_write_mail(e,true,0x5678));
        assert(gcn_dsp_engine_run(e,10000));
        const auto status_echo=consume(e);
        assert(status_echo==(0xfeee8000u|high));
        printf("status-echo high=%04x result=%08x\n",high,status_echo);
        gcn_dsp_engine_destroy(e);
    }
    puts("native DSP HLE boot-mail, high/low publication, ROM echo, consume and lifecycle passed");
}
