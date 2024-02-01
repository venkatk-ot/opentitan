CAPI=2:
# Copyright lowRISC contributors (OpenTitan project).
# Licensed under the Apache License, Version 2.0, see LICENSE for details.
# SPDX-License-Identifier: Apache-2.0
name: "lowrisc:ip:flash_ctrl_pkg:0.1"
description: "Flash Package"

filesets:
  files_rtl:
    depend:
      - lowrisc:constants:top_pkg
      - lowrisc:prim:util
      - lowrisc:ip:lc_ctrl_pkg
      - lowrisc:ip_interfaces:pwrmgr_pkg
      - lowrisc:ip_interfaces:flash_ctrl_reg
      - lowrisc:ip:jtag_pkg
      - lowrisc:ip:edn_pkg
      - lowrisc:tlul:headers
      - "fileset_partner  ? (partner:systems:ast_pkg)"
      - "!fileset_partner ? (lowrisc:systems:ast_pkg)"
    files:
      - rtl/flash_ctrl_pkg.sv
      - rtl/flash_phy_pkg.sv
    file_type: systemVerilogSource

  files_verilator_waiver:
    depend:
      # common waivers
      - lowrisc:lint:common
      - lowrisc:lint:comportable
    files:
      - lint/flash_ctrl_pkg.vlt
    file_type: vlt

  files_ascentlint_waiver:
    depend:
      # common waivers
      - lowrisc:lint:common
      - lowrisc:lint:comportable
    files:
      - lint/flash_ctrl_pkg.waiver
    file_type: waiver

  files_veriblelint_waiver:
    depend:
      # common waivers
      - lowrisc:lint:common
      - lowrisc:lint:comportable


targets:
  default:
    filesets:
      - tool_verilator   ? (files_verilator_waiver)
      - tool_ascentlint  ? (files_ascentlint_waiver)
      - tool_veriblelint ? (files_veriblelint_waiver)
      - files_rtl
