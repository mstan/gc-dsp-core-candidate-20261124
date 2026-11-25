// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp_core_state.hpp"
#include "Core/DSP/DSPCore.h"
#include "Core/DSP/DSPAccelerator.h"
#include "Core/DSP/DSPHost.h"
#include <cstring>

using namespace DSP;

// SNAPSHOT_RESUME pass A: static_assert the hand-rolled GcnDspLleSnapshot
// array sizes (dsp_lle_c.h) against the real compile-time constants, so a
// future upstream resync that changes DSP_IRAM_SIZE/DSP_DRAM_SIZE/
// DSP_STACK_DEPTH fails the build here instead of silently truncating a save.
static_assert(DSP_IRAM_SIZE == 4096, "GcnDspLleSnapshot::iram size mismatch");
static_assert(DSP_DRAM_SIZE == 4096, "GcnDspLleSnapshot::dram size mismatch");
static_assert(DSP_STACK_DEPTH == 32, "GcnDspLleSnapshot::reg_stacks size mismatch");

bool gcn_dsp_core_capture(DSPCore* core,GcnDspLleSnapshot* out,int pending) {
  if (!core || !out) return false;
  memset(out, 0, sizeof(*out));

  SDSP& dsp = core->DSPState();
  for (int i = 0; i < 4; i++) {
    out->r_ar[i] = dsp.r.ar[i];
    out->r_ix[i] = dsp.r.ix[i];
    out->r_wr[i] = dsp.r.wr[i];
    out->r_st[i] = dsp.r.st[i];
  }
  out->r_cr = dsp.r.cr;
  out->r_sr = dsp.r.sr;
  out->r_prod = dsp.r.prod.val;
  out->r_ax[0] = dsp.r.ax[0].val;
  out->r_ax[1] = dsp.r.ax[1].val;
  out->r_ac[0] = dsp.r.ac[0].val;
  out->r_ac[1] = dsp.r.ac[1].val;

  out->pc = dsp.pc;
  out->control_reg = dsp.control_reg;
  out->control_reg_init_code_clear_time = dsp.control_reg_init_code_clear_time;
  for (int i = 0; i < 4; i++) out->reg_stack_ptrs[i] = dsp.reg_stack_ptrs[i];
  out->exceptions = dsp.exceptions;
  out->external_interrupt_waiting = dsp.external_interrupt_waiting.load() ? 1 : 0;
  out->reset_dspjit_codespace = dsp.reset_dspjit_codespace ? 1 : 0;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < DSP_STACK_DEPTH; j++)
      out->reg_stacks[i][j] = dsp.reg_stacks[i][j];

  {
    const auto& ifx = dsp.IFXRegs();
    for (size_t i = 0; i < ifx.size() && i < 256; i++) out->ifx_regs[i] = ifx[i];
  }
  out->mailbox[0] = core->PeekMailbox(Mailbox::CPU);
  out->mailbox[1] = core->PeekMailbox(Mailbox::DSP);

  if (dsp.iram)
    for (uint32_t i = 0; i < DSP_IRAM_SIZE; i++) out->iram[i] = dsp.iram[i];
  if (dsp.dram)
    for (uint32_t i = 0; i < DSP_DRAM_SIZE; i++) out->dram[i] = dsp.dram[i];

  out->core_state = (int32_t)core->GetState();

  if (Accelerator* acc = dsp.GetAccelerator()) {
    out->accel_start_address = acc->GetStartAddress();
    out->accel_end_address = acc->GetEndAddress();
    out->accel_current_address = acc->GetCurrentAddress();
    out->accel_sample_format = acc->GetSampleFormat();
    out->accel_gain = acc->GetGain();
    out->accel_yn1 = acc->GetYn1();
    out->accel_yn2 = acc->GetYn2();
    out->accel_pred_scale = acc->GetPredScale();
    out->accel_input = acc->GetInput();
    out->accel_reads_stopped = acc->GetReadsStopped() ? 1 : 0;
  }

  out->dsp_int_pending = pending;
  return true;
}

bool gcn_dsp_core_restore(DSPCore* core,const GcnDspLleSnapshot* in) {
  if (!core || !in || in->core_state < int(State::Stopped) || in->core_state > int(State::Stepping))
    return false;
  for (uint8_t stack : in->reg_stack_ptrs)
    if (stack > DSP_STACK_MASK) return false;

  SDSP& dsp = core->DSPState();
  for (int i = 0; i < 4; i++) {
    dsp.r.ar[i] = in->r_ar[i];
    dsp.r.ix[i] = in->r_ix[i];
    dsp.r.wr[i] = in->r_wr[i];
    dsp.r.st[i] = in->r_st[i];
  }
  dsp.r.cr = in->r_cr;
  dsp.r.sr = in->r_sr;
  dsp.r.prod.val = in->r_prod;
  dsp.r.ax[0].val = in->r_ax[0];
  dsp.r.ax[1].val = in->r_ax[1];
  dsp.r.ac[0].val = in->r_ac[0];
  dsp.r.ac[1].val = in->r_ac[1];

  dsp.pc = in->pc;
  dsp.control_reg = in->control_reg;
  dsp.control_reg_init_code_clear_time = in->control_reg_init_code_clear_time;
  for (int i = 0; i < 4; i++) dsp.reg_stack_ptrs[i] = in->reg_stack_ptrs[i];
  dsp.exceptions = in->exceptions;
  dsp.external_interrupt_waiting.store(in->external_interrupt_waiting != 0);
  dsp.reset_dspjit_codespace = in->reset_dspjit_codespace != 0;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < DSP_STACK_DEPTH; j++)
      dsp.reg_stacks[i][j] = in->reg_stacks[i][j];

  {
    auto& ifx = dsp.IFXRegsMutable();
    for (size_t i = 0; i < ifx.size() && i < 256; i++) ifx[i] = in->ifx_regs[i];
  }
  dsp.SetMailboxRaw(Mailbox::CPU, in->mailbox[0]);
  dsp.SetMailboxRaw(Mailbox::DSP, in->mailbox[1]);

  if (dsp.iram)
    for (uint32_t i = 0; i < DSP_IRAM_SIZE; i++) dsp.iram[i] = in->iram[i];
  if (dsp.dram)
    for (uint32_t i = 0; i < DSP_DRAM_SIZE; i++) dsp.dram[i] = in->dram[i];

  core->SetState(static_cast<State>(in->core_state));

  if (Accelerator* acc = dsp.GetAccelerator()) {
    acc->SetStartAddress(in->accel_start_address);
    acc->SetEndAddress(in->accel_end_address);
    acc->SetCurrentAddress(in->accel_current_address);
    acc->SetSampleFormat(in->accel_sample_format);
    acc->SetGain(in->accel_gain);
    acc->SetYn1(in->accel_yn1);
    acc->SetYn2(in->accel_yn2);
    acc->SetPredScale(in->accel_pred_scale);
    acc->SetInput(in->accel_input);
    acc->SetReadsStopped(in->accel_reads_stopped != 0);
  }

  // Mirror DSPCore.cpp's own post-load DoState behavior (DSPCore.cpp:415-416,
  // `if (p.IsReadMode()) Host::CodeLoaded(...)`), which never fires in this
  // runtime because PointerWrap::IsReadMode() always returns false (see
  // Common/ChunkFile.h). Re-analyzing the restored IRAM here is what makes
  // the Analyzer's idle-skip/loop-detection tables valid again — without it
  // they'd still reflect whatever ucode (if any) ran before this restore.
  if (dsp.iram)
    DSP::Host::CodeLoaded(*core, reinterpret_cast<const u8*>(dsp.iram),
                          DSP_IRAM_BYTE_SIZE);

  return true;
}
