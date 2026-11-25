// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "gcnllr/native_dsp.h"
#include "Core/DSP/DSPCore.h"
#include <memory>
struct GcnDspEngine {
    GcnDspEngineConfig config{};
    std::unique_ptr<DSP::DSPCore> core;
    bool failed{};
};
extern GcnDspEngine* gcn_dsp_owner;
void gcn_dsp_engine_check(const GcnDspEngine& engine);
