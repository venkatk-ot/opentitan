// Copyright lowRISC contributors.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include "sw/ip/rom_ctrl/dif/dif_rom_ctrl.h"

#include "gtest/gtest.h"
#include "sw/ip/base/dif/dif_test_base.h"
#include "sw/lib/sw/device/base/mmio.h"
#include "sw/lib/sw/device/base/mock_mmio.h"

extern "C" {
#include "rom_ctrl_regs.h"  // Generated.
}  // extern "C"

namespace dif_rom_ctrl_test {
namespace {
using mock_mmio::MmioTest;
using mock_mmio::MockDevice;
using testing::Test;

// TODO: Add unit tests.

}  // namespace
}  // namespace dif_rom_ctrl_test