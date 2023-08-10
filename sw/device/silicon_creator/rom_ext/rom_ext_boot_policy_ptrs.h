// Copyright lowRISC contributors.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#ifndef OPENTITAN_SW_DEVICE_SILICON_CREATOR_ROM_EXT_ROM_EXT_BOOT_POLICY_PTRS_H_
#define OPENTITAN_SW_DEVICE_SILICON_CREATOR_ROM_EXT_ROM_EXT_BOOT_POLICY_PTRS_H_

#include "sw/lib/sw/device/silicon_creator/base/chip.h"
#include "sw/lib/sw/device/silicon_creator/manifest.h"
#include "sw/lib/sw/device/base/macros.h"

#include "hw/top_darjeeling/sw/autogen/top_darjeeling.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

static_assert((TOP_DARJEELING_EFLASH_SIZE_BYTES % 2) == 0,
              "Flash size is not divisible by 2");

#ifdef OT_PLATFORM_RV32
/**
 * Returns a pointer to the manifest of the first owner boot stage image stored
 * in flash slot A.
 *
 * @return Pointer to the manifest of the first owner boot stage image in slot
 * A.
 */
OT_WARN_UNUSED_RESULT
inline const manifest_t *rom_ext_boot_policy_manifest_a_get(void) {
  return (const manifest_t *)(TOP_DARJEELING_EFLASH_BASE_ADDR +
                              CHIP_ROM_EXT_SIZE_MAX);
}

/**
 * Returns a pointer to the manifest of the first owner boot stage image stored
 * in flash slot B.
 *
 * @return Pointer to the manifest of the first owner boot stage image in slot
 * B.
 */
OT_WARN_UNUSED_RESULT
inline const manifest_t *rom_ext_boot_policy_manifest_b_get(void) {
  return (const manifest_t *)(TOP_DARJEELING_EFLASH_BASE_ADDR +
                              (TOP_DARJEELING_EFLASH_SIZE_BYTES / 2) +
                              CHIP_ROM_EXT_SIZE_MAX);
}
#else
/**
 * Declarations for the functions above that should be defined in tests.
 */
const manifest_t *rom_ext_boot_policy_manifest_a_get(void);
const manifest_t *rom_ext_boot_policy_manifest_b_get(void);
#endif

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // OPENTITAN_SW_DEVICE_SILICON_CREATOR_ROM_EXT_ROM_EXT_BOOT_POLICY_PTRS_H_
