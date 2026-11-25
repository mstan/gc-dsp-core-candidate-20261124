// Copyright 2018 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Core/DSP/Interpreter/DSPInterpreter.h"

namespace DSP::Interpreter
{
// The MS C++ ABI chooses different member-pointer representations for an
// incomplete class. Every producer/consumer of the decoded table must see
// the complete Interpreter, regardless of its translation unit's include order.
using InterpreterFunction = void (Interpreter::*)(UDSPInstruction);

struct DecodedInterpreterOp
{
  InterpreterFunction main;
  InterpreterFunction extension;
};

InterpreterFunction GetOp(UDSPInstruction inst);
InterpreterFunction GetExtOp(UDSPInstruction inst);
const DecodedInterpreterOp& GetDecodedOp(UDSPInstruction inst);
void InitInstructionTables();
void FinalizeInstructionTables();
}  // namespace DSP::Interpreter
