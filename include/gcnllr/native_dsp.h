/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct GcnDspEngine GcnDspEngine;
typedef struct {
    uint8_t* mem1;uint32_t mem1_size;
    uint8_t* aram;uint32_t aram_size;
    const uint8_t* irom;uint32_t irom_size;
    const uint8_t* coef;uint32_t coef_size;
    void* user;
    /* Synchronous owner-thread spans. Writes notify code/reservation/journal
     * effects before bytes change. ARAM uses a separate address domain. */
    bool (*prepare)(void*,uint32_t offset,uint32_t bytes,bool writing);
    bool (*written)(void*,uint32_t offset,uint32_t bytes);
    uint64_t (*timebase)(void*);
    void (*interrupt)(void*);
    /* Optional Wii physical MEM2 bank. Zero initialization retains GameCube.
     * prepare/written receive physical addresses for both banks. */
    uint8_t* mem2;uint32_t mem2_size;
    bool wii;
} GcnDspEngineConfig;
typedef struct {
    uint64_t native_instructions,interpreter_instructions,misses,bindings;
    uint32_t provider,failed; /* 1 native HLE, 2 strict AOT, 3 development interpreter */
    uint16_t pc,control;
} GcnDspEngineStats;
GcnDspEngine* gcn_dsp_engine_create(const GcnDspEngineConfig*);
void gcn_dsp_engine_destroy(GcnDspEngine*);
/* Whether the selected engine can observe its next scheduled update. */
bool gcn_dsp_engine_update_pending(const GcnDspEngine*);
bool gcn_dsp_engine_run(GcnDspEngine*,uint32_t dsp_cycles);
bool gcn_dsp_engine_read_control(GcnDspEngine*,uint16_t*);
bool gcn_dsp_engine_write_control(GcnDspEngine*,uint16_t);
bool gcn_dsp_engine_read_mail(GcnDspEngine*,bool cpu,bool low,uint16_t*);
bool gcn_dsp_engine_write_mail(GcnDspEngine*,bool low,uint16_t);
/* Non-consuming diagnostic high half only. HLE's queue does not expose the
 * low half without popping it, so the common interface never promises it. */
uint16_t gcn_dsp_engine_peek_mail_high(const GcnDspEngine*,bool cpu);
GcnDspEngineStats gcn_dsp_engine_stats(const GcnDspEngine*);
/* Call on the device owner thread after settling pending owner work. The
 * byte buffer returned on success belongs to the caller. MEM1 and ARAM
 * remain caller-owned and have to be saved alongside these bytes. */
bool gcn_dsp_engine_capture(GcnDspEngine*,uint8_t** bytes,size_t* size);
bool gcn_dsp_engine_restore(GcnDspEngine*,const uint8_t* bytes,size_t size);
#ifdef __cplusplus
}
#endif
