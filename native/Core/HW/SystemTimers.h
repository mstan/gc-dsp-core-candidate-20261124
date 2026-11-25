// SPDX-License-Identifier: GPL-3.0-or-later
// Actual machine time replaces the vendored query-counted timer shim.
#pragma once
#include "Common/CommonTypes.h"
extern "C" uint64_t gcn_dsp_engine_timebase_v1(void);
class SystemTimersManager {
public:
    u64 GetFakeTimeBase() const {return gcn_dsp_engine_timebase_v1();}
};
