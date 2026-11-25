// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dsp_lle_c.h"

namespace DSP { class DSPCore; }

bool gcn_dsp_core_capture(DSP::DSPCore*,GcnDspLleSnapshot*,int pending);
bool gcn_dsp_core_restore(DSP::DSPCore*,const GcnDspLleSnapshot*);
