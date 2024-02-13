CAPI=2:
# Copyright lowRISC contributors.
# Licensed under the Apache License, Version 2.0, see LICENSE for details.
# SPDX-License-Identifier: Apache-2.0
${gen_core_comment}
name: "lowrisc:fpv:flash_ctrl_csr_assert_gen:0.1"
description: "FLASH_CTRL generated csr assertions."
filesets:
  files_dv:
    depend:
      - lowrisc:fpv:csr_assert_gen

generate:
  csr_assert_gen:
    generator: csr_assert_gen
    parameters:
      spec: ../../../data/autogen/flash_ctrl.hjson

targets:
  default: &default_target
    filesets:
      - files_dv
    generate:
      - csr_assert_gen