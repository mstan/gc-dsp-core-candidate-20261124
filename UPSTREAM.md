# Vendored DSP LLE core

`dsp/` is Dolphin's GameCube/Wii DSP low-level-emulation **interpreter** core,
vendored from `oracle/dolphin/Source/Core/Core/DSP/` (GPL-2.0-or-later, which we
carry under this project's GPL-3.0). It runs the real DSP firmware
(`bios/dsp_rom.bin` IROM + `bios/dsp_coef.bin` DROM) plus the ucode the IPL
uploads — i.e. genuine LLE of the DSP, not an HLE stub.

The vendored files were taken from
[Dolphin commit `6de526c684ee6dd8b6fc3447dae858ae480ccbb5`](https://github.com/dolphin-emu/dolphin/commit/6de526c684ee6dd8b6fc3447dae858ae480ccbb5)
(checked out 2026-07-09). Original Dolphin copyright and
`GPL-2.0-or-later` SPDX headers are retained in the imported files.

Included: DSPCore, DSPTables, DSPHWInterface, DSPAccelerator, DSPMemoryMap,
DSPStacks, DSPAnalyzer, DSPCaptureLogger, and `Interpreter/` (Int{Arithmetic,
Multiplier,ExtOps,Branch,LoadStore,Tables,CCUtil}). Excluded: `Jit/` (we use the
interpreter — deterministic), Assembler/Disassembler (not needed to execute).

The release/dev build uses GCC LTO on this small C++ library only
(`GCN_DSP_LTO=ON`, disable-able). It lets the compiler inline between the
interpreter's opcode-family translation units; it is not a DSP JIT and does
not skip or replace firmware instructions.

Integration (net-new, ours):
- `host.cpp` implements the `DSP::Host::*` callbacks against the runtime's MEM1
  (ReadHostMemory/WriteHostMemory/DMAToDSP/DMAFromDSP) and PI (InterruptRequest).
- `compat/` provides minimal shims for the handful of Dolphin Common/Core
  headers the interpreter pulls in (CommonTypes, Logging, MemoryUtil, Event,
  Assert, plus a 4-call-site SystemTimers/CoreTiming timing shim for the
  DSPInitCode window).
- `dsp_lle_c.{h,cpp}` exposes a C API (init/reset/run/mailbox/control-reg) that
  `runtime/src/dsp.c` wires the 0xCC005000 MMIO block to.

The DSP AR/AI DMA engine (0xCC005020+) stays in `dsp.c` — that is the Flipper
ARAM DMA (CPU-side hardware), not the DSP core.

Local modification, 2026-10-24: `SDSP::CheckExceptions` reevaluates a pending
`CR_EXTERNAL_INT` when the DSP unmasks external interrupts. The CPU request
must not require a second CSR write. The native/LLE regression covers masked
retention, delivery/return PC and exactly-once behavior. The real integrated
Wind Waker route advances past its former JAudio command-mailbox stall.

Validation is bounded evidence, not correctness by ancestry. The boot handshake
is checked against observed mailbox/interrupt behavior and firmware execution;
native DSP is compared with the functioning LLE implementation. Matching these
two implementations alone is not independent hardware validation. See
`docs/WIND-WAKER-BOOT.md` for the exercised route and remaining gates.
