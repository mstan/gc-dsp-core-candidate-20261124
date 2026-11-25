// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdio>
#include <set>
#include <utility>
#include "Common/CommonTypes.h"
namespace DSP {class DSPCore;struct SDSP;}
namespace DSP::Aot {
using Body=void(*)(DSPCore&);
struct Image {
  u16 base;u32 count;u64 hash;const u16* words;const Body* bodies;const u8* sizes;const char* sha256;
};
struct Catalog {const Image* images;size_t count;};
struct Stats {u64 native=0,misses=0,bindings=0,rejected=0;bool failed=false;};
Catalog GetCatalog();
u64 CodeHash(const u16* words,size_t count);
void CaptureCode(const SDSP& state,u16 base);
class Engine {
public:
  Engine(DSPCore& core,Catalog catalog,bool strict);
  ~Engine();
  int RunCycles(int cycles);
  bool StepOnce() {return Step();}
  void BindIRAM();
  bool SkipKnownInstruction();
  const Stats& GetStats() const {return m_stats;}
  void Report();
private:
  const Image* Bind(u16 base);
  bool Step();
  DSPCore& m_core;
  Catalog m_catalog;
  const bool m_strict;
  const Image* m_rom=nullptr;
  const Image* m_ram=nullptr;
  u64 m_rom_hash=0,m_ram_hash=0;
  Stats m_stats;
  std::set<std::pair<u64,u16>> m_seen;
  FILE* m_journal=nullptr;
  bool m_reported=false;
};
}
