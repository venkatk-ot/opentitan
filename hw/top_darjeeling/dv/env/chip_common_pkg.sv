// Copyright lowRISC contributors.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

// Provides parameters, types and methods shared throughout the chip level testbench.
package chip_common_pkg;

  import dv_utils_pkg::uint;

  // Chip composition (number of hardware resources).
  parameter dv_utils_pkg::uint NUM_GPIOS = 32;
  parameter dv_utils_pkg::uint NUM_UARTS = 1;
  parameter dv_utils_pkg::uint NUM_SPI_HOSTS = 1;
  parameter dv_utils_pkg::uint NUM_I2CS = 1;

  // SW constants - use unmapped address space with at least 32 bytes.
  parameter bit [top_pkg::TL_AW-1:0] SW_DV_START_ADDR = tl_main_pkg::ADDR_SPACE_RV_CORE_IBEX__CFG +
      rv_core_ibex_reg_pkg::RV_CORE_IBEX_DV_SIM_WINDOW_OFFSET;

  parameter bit [top_pkg::TL_AW-1:0] SW_DV_TEST_STATUS_ADDR = SW_DV_START_ADDR + 0;
  parameter bit [top_pkg::TL_AW-1:0] SW_DV_LOG_ADDR         = SW_DV_START_ADDR + 4;

  parameter uint ROM_CONSOLE_UART = 0;

  // ROM Boot Fault Values, matches definitions in `rules/const.bzl`.
  parameter string ROM_BFV_BAD_IDENTIFIER     = "0142500d";
  parameter string ROM_BFV_BAD_RSA_SIGNATURE    = "01535603";
  parameter string ROM_BFV_INSTRUCTION_ACCESS = "01495202";

  // ROM Lifecycle Values, matches definitions in `rules/const.bzl`.
  parameter string ROM_LCV_TEST_UNLOCKED0 = "02108421";
  parameter string ROM_LCV_DEV            = "21084210";
  parameter string ROM_LCV_PROD           = "2318c631";
  parameter string ROM_LCV_PROD_END       = "25294a52";
  parameter string ROM_LCV_RMA            = "2739ce73";

  string lc_state_2_rom_lcv[lc_ctrl_state_pkg::lc_state_e] = '{
      lc_ctrl_state_pkg::LcStTestUnlocked0: ROM_LCV_TEST_UNLOCKED0,
      lc_ctrl_state_pkg::LcStDev: ROM_LCV_DEV,
      lc_ctrl_state_pkg::LcStProd: ROM_LCV_PROD,
      lc_ctrl_state_pkg::LcStProdEnd: ROM_LCV_PROD_END,
      lc_ctrl_state_pkg::LcStRma: ROM_LCV_RMA};

  // Auto-generated parameters. TODO: rename to chip_common_pkg__params.svh.
  `include "autogen/chip_env_pkg__params.sv"

  // TODO: Eventually, move everything from chip_env_pkg to here.

  // Represents the clock source used by the chip during simulations.
  //
  // It is indicative of both, the source of the clock used for the test, as well as the frequency
  // in MHz (the literal value).
  typedef enum {
    // Use the internal clocks generated by the AST. This is the default for most tests.
    ChipClockSourceInternal = 0,

    // Use the external clock source with 48MHz frequency. This requires chip_if::ext_clk_if to be
    // connected.
    ChipClockSourceExternal48Mhz = 48,

    // Use the external clock source with 98MHz frequency (nominal). This requires
    // the chip_if::ext_clk_if to be connected.
    ChipClockSourceExternal96Mhz = 96
  } chip_clock_source_e;

  // Represents the various chip-wide control signals broadcast by the LC controller.
  //
  // The design emits these as a redundantly encoded signal of type lc_ctrl_pkg::lc_tx_t, which can
  // be compared against the {On, Off} values.
  typedef enum {
    LcCtrlSignalDftEn,
    LcCtrlSignalNvmDebugEn,
    LcCtrlSignalHwDebugEn,
    LcCtrlSignalCpuEn,
    LcCtrlSignalCreatorSeedEn,
    LcCtrlSignalOwnerSeedEn,
    LcCtrlSignalIsoRdEn,
    LcCtrlSignalIsoWrEn,
    LcCtrlSignalSeedRdEn,
    LcCtrlSignalKeyMgrEn,
    LcCtrlSignalEscEn,
    LcCtrlSignalCheckBypEn,
    LcCtrlSignalNumTotal
  } lc_ctrl_signal_e;

  typedef enum bit [1:0] {
    JtagTapNone = 2'b00,
    JtagTapLc = 2'b01,
    JtagTapRvDm = 2'b10,
    JtagTapDft = 2'b11
  } chip_jtag_tap_e;

  // This maps the DIO on the pinmux / peripheral side to the DIO on the pad side, both of
  // which have different enum numbering in top_darjeeling_pkg.sv.
  parameter top_darjeeling_pkg::dio_pad_e DioToDioPadMap [top_darjeeling_pkg::DioCount] = '{
    top_darjeeling_pkg::DioPadSpiHostD0,  /* DioSpiHost0Sd0 */
    top_darjeeling_pkg::DioPadSpiHostD1,  /* DioSpiHost0Sd1 */
    top_darjeeling_pkg::DioPadSpiHostD2,  /* DioSpiHost0Sd2 */
    top_darjeeling_pkg::DioPadSpiHostD3,  /* DioSpiHost0Sd3 */
    top_darjeeling_pkg::DioPadSpiDevD0,   /* DioSpiDeviceSd0 */
    top_darjeeling_pkg::DioPadSpiDevD1,   /* DioSpiDeviceSd1 */
    top_darjeeling_pkg::DioPadSpiDevD2,   /* DioSpiDeviceSd2 */
    top_darjeeling_pkg::DioPadSpiDevD3,   /* DioSpiDeviceSd3 */
    top_darjeeling_pkg::DioPadSpiDevClk,  /* DioSpiDeviceSck */
    top_darjeeling_pkg::DioPadSpiDevCsL,  /* DioSpiDeviceCsb */
    top_darjeeling_pkg::DioPadSpiHostClk, /* DioSpiHost0Sck */
    top_darjeeling_pkg::DioPadSpiHostCsL  /* DioSpiHost0Csb */
  };

  typedef struct packed {
    lc_ctrl_state_pkg::lc_state_e lc_state;
    lc_ctrl_state_pkg::dec_lc_state_e dec_lc_state;
  } lc_state_t;

  parameter lc_state_t UnlockedStates[8] = '{
    '{
      lc_state: lc_ctrl_state_pkg::LcStTestUnlocked0,
      dec_lc_state: lc_ctrl_state_pkg::DecLcStTestUnlocked0
     },
    '{
      lc_state: lc_ctrl_state_pkg::LcStTestUnlocked1,
      dec_lc_state: lc_ctrl_state_pkg::DecLcStTestUnlocked1
     },
    '{
      lc_state: lc_ctrl_state_pkg::LcStTestUnlocked2,
      dec_lc_state: lc_ctrl_state_pkg::DecLcStTestUnlocked2
     },
    '{
      lc_state: lc_ctrl_state_pkg::LcStTestUnlocked3,
      dec_lc_state: lc_ctrl_state_pkg::DecLcStTestUnlocked3
     },
    '{
      lc_state: lc_ctrl_state_pkg::LcStTestUnlocked4,
      dec_lc_state: lc_ctrl_state_pkg::DecLcStTestUnlocked4
     },
    '{
      lc_state: lc_ctrl_state_pkg::LcStTestUnlocked5,
      dec_lc_state: lc_ctrl_state_pkg::DecLcStTestUnlocked5
     },
    '{
      lc_state: lc_ctrl_state_pkg::LcStTestUnlocked6,
      dec_lc_state: lc_ctrl_state_pkg::DecLcStTestUnlocked6
     },
    '{
      lc_state: lc_ctrl_state_pkg::LcStTestUnlocked7,
      dec_lc_state: lc_ctrl_state_pkg::DecLcStTestUnlocked7
     }
  };

endpackage
