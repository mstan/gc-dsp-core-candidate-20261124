/* SPDX-License-Identifier: MIT */
#ifndef GCN_PRIVATE_DSP_DMA_REJECT_TRACE_H
#define GCN_PRIVATE_DSP_DMA_REJECT_TRACE_H
#include <cstdint>
#include <cstdio>

namespace gcnllr::diagnostic {
struct DmaReject {
    std::uint16_t pc,control,local_word,length,main_high,main_low;
    std::uint32_t capacity;
};
/* Diagnostic arithmetic only. In particular, legacy_byte reports the old
 * implementation's narrowing; it does not authorize a hardware mirror. */
inline void write_dma_reject(std::FILE* out,const DmaReject& s) {
    const std::uint32_t local=std::uint32_t(s.local_word)*2u;
    const std::uint32_t end=local+s.length;
    const std::uint32_t main=(std::uint32_t(s.main_high)<<16)|s.main_low;
    const bool address_oob=local>s.capacity;
    const bool crossing=!address_oob && s.length>s.capacity-local;
    const bool odd=(s.length&1u)!=0;
    const char* first=address_oob ? "local-address" : crossing ? "local-span" :
                      odd ? "odd-length" : "none";
    std::fprintf(out,
        "[native-dsp-dma-reject] pc=0x%04X ctl=0x%04X mode=%u bank=%s direction=%s "
        "local_word=0x%04X local_byte=0x%08X local_end=0x%08X capacity=0x%08X "
        "length=0x%04X main=0x%08X main_hi=0x%04X main_lo=0x%04X "
        "legacy_byte=0x%04X first=%s address_oob=%u crossing=%u odd=%u\n",
        unsigned(s.pc),unsigned(s.control),unsigned(s.control&3u),
        (s.control&2u) ? "IRAM" : "DRAM",(s.control&1u) ? "to-CPU" : "from-CPU",
        unsigned(s.local_word),unsigned(local),unsigned(end),unsigned(s.capacity),
        unsigned(s.length),unsigned(main),unsigned(s.main_high),unsigned(s.main_low),
        unsigned(std::uint16_t(local)),first,unsigned(address_oob),unsigned(crossing),unsigned(odd));
}
} // namespace gcnllr::diagnostic
#endif
