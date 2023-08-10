// Copyright lowRISC contributors.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include "sw/lib/sw/device/silicon_creator/epmp_state.h"
#include "sw/lib/sw/device/base/macros.h"

// In-memory copy of the ePMP register configuration.
//
// This is placed at a fixed location in memory within the .static_critical
// section. It will be populated by the ROM before the jump to ROM_EXT.
OT_SET_BSS_SECTION(".static_critical.epmp_state", epmp_state_t epmp_state;)
