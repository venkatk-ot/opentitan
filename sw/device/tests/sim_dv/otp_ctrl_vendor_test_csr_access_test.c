// Copyright lowRISC contributors.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include "sw/device/lib/testing/test_framework/check.h"
#include "sw/device/lib/testing/test_framework/ottf_test_config.h"
#include "sw/ip/lc_ctrl/dif/dif_lc_ctrl.h"
#include "sw/ip/otp_ctrl/dif/dif_otp_ctrl.h"
#include "sw/lib/sw/device/base/memory.h"
#include "sw/lib/sw/device/base/mmio.h"
#include "sw/lib/sw/device/runtime/log.h"
#include "sw/lib/sw/device/silicon_creator/base/chip.h"

#include "hw/top_darjeeling/sw/autogen/top_darjeeling.h"

static dif_lc_ctrl_t lc;
static dif_otp_ctrl_t otp;

OTTF_DEFINE_TEST_CONFIG();

static void init_peripherals(void) {
  dif_otp_ctrl_config_t config = {
      .check_timeout = 100000,
      .integrity_period_mask = 0x3ffff,
      .consistency_period_mask = 0x3ffffff,
  };
  // Life cycle
  CHECK_DIF_OK(dif_lc_ctrl_init(
      mmio_region_from_addr(TOP_DARJEELING_LC_CTRL_BASE_ADDR), &lc));
  // OTP
  CHECK_DIF_OK(dif_otp_ctrl_init(
      mmio_region_from_addr(TOP_DARJEELING_OTP_CTRL_CORE_BASE_ADDR), &otp));
  CHECK_DIF_OK(dif_otp_ctrl_configure(&otp, config));
}

/**
 * A simple SW test to enable OTP_CTRL and LC_CTRL.
 * The main sequence is driven by JTAG agent in SV sequence
 * `chip_sw_otp_ctrl_vendor_test_csr_access_vseq.sv`.
 */
bool test_main(void) {
  init_peripherals();
  return true;
}
