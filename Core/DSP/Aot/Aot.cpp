// SPDX-License-Identifier: MIT
#include "Core/DSP/Aot/Aot.h"
#include "Core/DSP/DSPCore.h"
#include "Core/DSP/DSPAnalyzer.h"
#include "Core/DSP/Interpreter/DSPInterpreter.h"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

namespace DSP::Aot {
#ifndef GCNLLR_DSP_AOT_CATALOG
Catalog GetCatalog() {return {nullptr,0};}
#endif
u64 CodeHash(const u16* words,size_t count) {
  u64 hash=0xcbf29ce484222325ULL;
  for(size_t i=0;i<count;i++) {
    hash=(hash^(words[i]>>8))*0x100000001b3ULL;
    hash=(hash^(words[i]&255))*0x100000001b3ULL;
  }
  return hash;
}
void CaptureCode(const SDSP& state,u16 base) {
  static const char* directory=std::getenv("GCN_DSP_AOT_CAPTURE");
  if(!directory || !*directory)return;
  const u16* words=base ? state.irom : state.iram;
  const size_t count=base ? size_t(DSP_IROM_SIZE) : size_t(DSP_IRAM_SIZE);
  if(!words)return;
  std::vector<unsigned char> bytes(count*2);
  for(size_t i=0;i<count;i++){bytes[i*2]=words[i]>>8;bytes[i*2+1]=words[i];}
  std::error_code error;std::filesystem::create_directories(directory,error);
  if(error){std::fprintf(stderr,"[dsp-aot] cannot create capture directory: %s\n",error.message().c_str());return;}
  char name[96];std::snprintf(name,sizeof(name),"%s-%016llx.bin",base ? "irom" : "iram",(unsigned long long)CodeHash(words,count));
  const auto path=std::filesystem::path(directory)/name;
  if(std::filesystem::exists(path)) {
    FILE* previous=std::fopen(path.string().c_str(),"rb");
    std::vector<unsigned char> check(bytes.size());
    const bool same=previous && std::fread(check.data(),1,check.size(),previous)==check.size() && check==bytes && std::fgetc(previous)==EOF && !std::ferror(previous);
    if(previous)std::fclose(previous);
    if(!same)std::fprintf(stderr,"[dsp-aot] capture identity collision/unreadable file: %s\n",path.string().c_str());
    return;
  }
  static unsigned captures;
  if(captures>=64){std::fprintf(stderr,"[dsp-aot] bounded code capture limit reached\n");return;}
  const auto temporary=path.string()+".tmp";
  FILE* file=std::fopen(temporary.c_str(),"wb");
  if(!file){std::fprintf(stderr,"[dsp-aot] cannot write capture %s\n",temporary.c_str());return;}
  const bool written=std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size();
  const bool closed=std::fclose(file)==0;
  if(!written || !closed || std::rename(temporary.c_str(),path.string().c_str())) {
    std::remove(temporary.c_str());std::fprintf(stderr,"[dsp-aot] code capture failed\n");return;
  }
  captures++;
  std::fprintf(stderr,"[dsp-aot] captured %s\n",path.string().c_str());
}
Engine::Engine(DSPCore& core,Catalog catalog,bool strict):m_core(core),m_catalog(catalog),m_strict(strict) {
  const char* journal=std::getenv("GCN_DSP_AOT_JOURNAL");
  if(journal && *journal)m_journal=std::fopen(journal,"ab");
  if(journal && *journal && !m_journal)std::fprintf(stderr,"[dsp-aot] cannot append journal %s\n",journal);
  if(m_journal){std::fprintf(m_journal,"{\"event\":\"capture_begin\",\"backend\":\"aot\",\"strict\":%s}\n",m_strict ? "true":"false");std::fflush(m_journal);}
  m_rom=Bind(0x8000);m_ram=Bind(0);
  std::fprintf(stderr,"[dsp-aot] static catalog images=%zu strict=%d; no runtime code generation\n",catalog.count,int(strict));
}
Engine::~Engine(){Report();}
const Image* Engine::Bind(u16 base) {
  const auto& state=m_core.DSPState();
  const u16* words=base ? state.irom : state.iram;
  const size_t count=base ? size_t(DSP_IROM_SIZE):size_t(DSP_IRAM_SIZE);
  const u64 hash=CodeHash(words,count);
  if(base)m_rom_hash=hash;else m_ram_hash=hash;
  const Image* selected=nullptr;
  for(size_t i=0;i<m_catalog.count;i++) {
    const Image& image=m_catalog.images[i];
    if(image.base!=base || image.count!=count || image.hash!=hash)continue;
    if(!std::memcmp(words,image.words,count*sizeof(u16))) {selected=&image;break;}
    m_stats.rejected++;
  }
  m_stats.bindings++;
  if(m_journal){std::fprintf(m_journal,"{\"event\":\"code_binding\",\"base\":%u,\"hash\":\"%016llx\",\"compiled\":%s}\n",base,(unsigned long long)hash,selected ? "true":"false");std::fflush(m_journal);}
  return selected;
}
void Engine::BindIRAM(){m_ram=Bind(0);}
bool Engine::SkipKnownInstruction() {
  auto& state=m_core.DSPState();
  const Image* image=(state.pc>>12)==8 ? m_rom : (state.pc>>12)==0 ? m_ram : nullptr;
  if(!image)return false;
  state.pc+=image->sizes[state.pc-image->base];return true;
}
bool Engine::Step() {
  m_core.CheckExceptions();
  auto& state=m_core.DSPState();
  const u16 pc=state.pc;
  const Image* image=(pc>>12)==8 ? m_rom : (pc>>12)==0 ? m_ram : nullptr;
  if(image && image->bodies[pc-image->base]) {
    image->bodies[pc-image->base](m_core);m_stats.native++;return true;
  }
  const u64 hash=(pc>>12)==8 ? m_rom_hash : m_ram_hash;
  if(m_seen.emplace(hash,pc).second) {
    m_stats.misses++;
    const u16 raw=state.ReadIMEM(pc);
    std::fprintf(stderr,"[dsp-aot-miss] pc=%04x raw=%04x image=%016llx policy=%s\n",pc,raw,(unsigned long long)hash,m_strict ? "stop":"interpreter");
    if(m_journal){std::fprintf(m_journal,"{\"event\":\"native_miss\",\"pc\":%u,\"raw\":%u,\"image_hash\":\"%016llx\"}\n",pc,raw,(unsigned long long)hash);std::fflush(m_journal);}
  }
  if(m_strict){m_stats.failed=true;return false;}
  // Exceptions were checked once above; a second check could deliver another
  // exception before the first vector instruction. Use the counted fallback.
  m_core.GetInterpreter().StepAfterExceptions();return true;
}
int Engine::RunCycles(int cycles) {
  auto& state=m_core.DSPState();
  // Preserve the floor's budget/idle cadence: initial8, idle-checked8,
  // unguarded200, then repeat the latter two phases.
  unsigned phase=0,index=0;
  while(cycles>0) {
    if(phase<2 && (state.control_reg&CR_HALT))return 0;
    if(phase==1 && state.GetAnalyzer().IsIdleSkip(state.pc))return 0;
    if(!Step())return cycles;
    cycles--;
    if(++index==(phase==2 ? 200u:8u)){index=0;phase=phase==1 ? 2:1;}
  }
  return cycles;
}
void Engine::Report() {
  if(m_reported)return;m_reported=true;
  const u64 interpreted=m_core.GetInterpreter().InterpretedInstructions();
  if(m_journal){std::fprintf(m_journal,"{\"event\":\"summary\",\"native_instructions\":%llu,\"interpreter_instructions\":%llu,\"unique_misses\":%llu,\"bindings\":%llu,\"failed\":%s}\n",
      (unsigned long long)m_stats.native,(unsigned long long)interpreted,(unsigned long long)m_stats.misses,(unsigned long long)m_stats.bindings,m_stats.failed ? "true":"false");std::fclose(m_journal);m_journal=nullptr;}
  std::fprintf(stderr,"[dsp-aot] summary native=%llu interpreter=%llu unique-misses=%llu bindings=%llu failed=%d\n",
      (unsigned long long)m_stats.native,(unsigned long long)interpreted,(unsigned long long)m_stats.misses,(unsigned long long)m_stats.bindings,int(m_stats.failed));
}
}
