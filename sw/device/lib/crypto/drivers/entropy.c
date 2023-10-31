// Copyright lowRISC contributors.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include "sw/device/lib/crypto/drivers/entropy.h"

#include "sw/device/lib/base/abs_mmio.h"
#include "sw/device/lib/base/bitfield.h"
#include "sw/device/lib/base/math.h"
#include "sw/device/lib/base/memory.h"
#include "sw/device/lib/base/multibits.h"
#include "sw/device/lib/crypto/impl/status.h"

#include "csrng_regs.h"        // Generated
#include "edn_regs.h"          // Generated
#include "entropy_src_regs.h"  // Generated
#include "hw/top_earlgrey/sw/autogen/top_earlgrey.h"

// Module ID for status codes.
#define MODULE_ID MAKE_MODULE_ID('d', 'e', 'n')

enum {
  kBaseCsrng = TOP_EARLGREY_CSRNG_BASE_ADDR,
  kBaseEntropySrc = TOP_EARLGREY_ENTROPY_SRC_BASE_ADDR,
  kBaseEdn0 = TOP_EARLGREY_EDN0_BASE_ADDR,
  kBaseEdn1 = TOP_EARLGREY_EDN1_BASE_ADDR,

  /**
   * CSRNG genbits buffer size in uint32_t words.
   */
  kEntropyCsrngBitsBufferNumWords = 4,
};

/**
 * Supported CSRNG application commands.
 * See https://docs.opentitan.org/hw/ip/csrng/doc/#command-header for
 * details.
 */
// TODO(#14542): Harden csrng/edn command fields.
typedef enum entropy_csrng_op {
  kEntropyDrbgOpInstantiate = 1,
  kEntropyDrbgOpReseed = 2,
  kEntropyDrbgOpGenerate = 3,
  kEntropyDrbgOpUpdate = 4,
  kEntropyDrbgOpUninstantiate = 5,
} entropy_csrng_op_t;

/**
 * CSRNG application interface command header parameters.
 */
typedef struct entropy_csrng_cmd {
  /**
   * Application command ID.
   */
  entropy_csrng_op_t id;
  /**
   * Entropy source enable.
   *
   * Mapped to flag0 in the hardware command interface.
   */
  hardened_bool_t disable_trng_input;
  const entropy_seed_material_t *seed_material;
  /**
   * Generate length. Specified as number of 128bit blocks.
   */
  uint32_t generate_len;
} entropy_csrng_cmd_t;

/**
 * Entropy complex configuration modes.
 *
 * Each enum value is used a confiugration index in `kEntropyComplexConfigs`.
 */
typedef enum entropy_complex_config_id {
  /**
   * Entropy complex in continuous mode. This is the default runtime
   * configuration.
   */
  kEntropyComplexConfigIdContinuous,
  kEntropyComplexConfigIdNumEntries,
} entropy_complex_config_id_t;

/**
 * EDN configuration settings.
 */
typedef struct edn_config {
  /**
   * Base address of the EDN block.
   */
  uint32_t base_address;
  /**
   * Number of generate calls between reseed commands.
   */
  uint32_t reseed_interval;
  /**
   * Downstream CSRNG instantiate command configuration.
   */
  entropy_csrng_cmd_t instantiate;
  /**
   * Downstream CSRNG generate command configuration.
   */
  entropy_csrng_cmd_t generate;
  /**
   * Downstream CSRNG reseed command configuration.
   */
  entropy_csrng_cmd_t reseed;
} edn_config_t;

/**
 * Entropy source configuration settings.
 */
typedef struct entropy_src_config {
  /**
   * If set, FIPS compliant entropy will be generated by this module after being
   * processed by an SP 800-90B compliant conditioning function.
   */
  multi_bit_bool_t fips_enable;
  /**
   * If set, entropy will be routed to a firmware-visible register instead of
   * being distributed to other hardware IPs.
   */
  multi_bit_bool_t route_to_firmware;
  /**
   * If set, raw entropy will be sent to CSRNG, bypassing the conditioner block
   * and disabling the FIPS hardware generated flag.
   */
  multi_bit_bool_t bypass_conditioner;
  /**
   * Enables single bit entropy mode.
   */
  multi_bit_bool_t single_bit_mode;
  /**
   * The size of the window used for health tests.
   */
  uint16_t fips_test_window_size;
  /**
   * The number of health test failures that must occur before an alert is
   * triggered. When set to 0, alerts are disabled.
   */
  uint16_t alert_threshold;
  /**
   * Repetition count test threshold.
   */
  uint16_t repcnt_threshold;
  /**
   * Repetition count symbol test threshold.
   */
  uint16_t repcnts_threshold;
  /**
   * Adaptive proportion test high threshold.
   */
  uint16_t adaptp_hi_threshold;
  /**
   * Adaptive proportion test low threshold.
   */
  uint16_t adaptp_lo_threshold;
  /**
   * Bucket test threshold.
   */
  uint16_t bucket_threshold;
  /**
   * Markov test high threshold.
   */
  uint16_t markov_hi_threshold;
  /**
   * Markov test low threshold.
   */
  uint16_t markov_lo_threshold;
  /**
   * External health test high threshold.
   */
  uint16_t extht_hi_threshold;
  /**
   * External health test low threshold.
   */
  uint16_t extht_lo_threshold;
} entropy_src_config_t;

/**
 * Entropy complex configuration settings.
 *
 * Contains configuration paramenters for entropy_src, csrng, edn0 and edn1.
 */
typedef struct entropy_complex_config {
  /**
   * Configuration identifier.
   */
  entropy_complex_config_id_t id;
  /**
   * ENTROPY_SRC configuration.
   */
  entropy_src_config_t entropy_src;
  /**
   * EDN0 configuration.
   */
  edn_config_t edn0;
  /**
   * EDN1 configuration.
   */
  edn_config_t edn1;
} entropy_complex_config_t;

// Entropy complex configuration table. This is expected to be fixed at build
// time. For this reason, it is not recommended to use this table in a ROM
// target unless the values are known to work. In other words, only use in
// mutable code partitions.
static const entropy_complex_config_t
    kEntropyComplexConfigs[kEntropyComplexConfigIdNumEntries] = {
        [kEntropyComplexConfigIdContinuous] =
            {
                .entropy_src =
                    {
                        .fips_enable = kMultiBitBool4True,
                        .route_to_firmware = kMultiBitBool4False,
                        .bypass_conditioner = kMultiBitBool4False,
                        .single_bit_mode = kMultiBitBool4False,
                        .fips_test_window_size = 0x200,
                        .alert_threshold = 2,
                        // TODO(#19392): Figure out appropriate thresholds.
                        .repcnt_threshold = 0xffff,
                        .repcnts_threshold = 0xffff,
                        .adaptp_hi_threshold = 0xffff,
                        .adaptp_lo_threshold = 0x0,
                        .bucket_threshold = 0xffff,
                        .markov_hi_threshold = 0xffff,
                        .markov_lo_threshold = 0x0,
                        .extht_hi_threshold = 0xffff,
                        .extht_lo_threshold = 0x0,
                    },
                .edn0 =
                    {
                        .base_address = kBaseEdn0,
                        .reseed_interval = 32,
                        .instantiate =
                            {
                                .id = kEntropyDrbgOpInstantiate,
                                .disable_trng_input = kHardenedBoolFalse,
                                .seed_material = NULL,
                                .generate_len = 0,
                            },
                        .generate =
                            {
                                .id = kEntropyDrbgOpGenerate,
                                .disable_trng_input = kHardenedBoolFalse,
                                .seed_material = NULL,
                                .generate_len = 8,
                            },
                        .reseed =
                            {
                                .id = kEntropyDrbgOpReseed,
                                .disable_trng_input = kHardenedBoolFalse,
                                .seed_material = NULL,
                                .generate_len = 0,
                            },
                    },
                .edn1 =
                    {
                        .base_address = kBaseEdn1,
                        .reseed_interval = 4,
                        .instantiate =
                            {
                                .id = kEntropyDrbgOpInstantiate,
                                .disable_trng_input = kHardenedBoolFalse,
                                .seed_material = NULL,
                                .generate_len = 0,
                            },
                        .generate =
                            {
                                .id = kEntropyDrbgOpGenerate,
                                .seed_material = NULL,
                                .generate_len = 1,
                            },
                        .reseed =
                            {
                                .id = kEntropyDrbgOpReseed,
                                .disable_trng_input = kHardenedBoolFalse,
                                .seed_material = NULL,
                                .generate_len = 0,
                            },
                    },
            },
};

// TODO(#19568): CSRNG commands may hang if the main FSM is not idle.
// This function is used as a workaround to poll for the internal FSM state,
// blocking until it reaches the `kCsrngMainSmIdle` state. The function
// attempts `kCsrngIdleNumTries` before returning `OTCRYPTO_RECOV_ERR` if
// unable to detect idle state.
OT_WARN_UNUSED_RESULT
static status_t csrng_fsm_idle_wait(void) {
  enum {
    kCsrngIdleNumTries = 100000,

    // This value needs to match `MainSmIdle` in csrng_pkg.sv.
    kCsrngMainSmIdle = 0x4e,
  };
  for (size_t i = 0; i < kCsrngIdleNumTries; ++i) {
    uint32_t reg = abs_mmio_read32(kBaseCsrng + CSRNG_MAIN_SM_STATE_REG_OFFSET);
    if (reg == kCsrngMainSmIdle) {
      return OTCRYPTO_OK;
    }
  }
  return OTCRYPTO_RECOV_ERR;
}

// Write a CSRNG command to a register.  That register can be the SW interface
// of CSRNG, in which case the `check_completion` argument should be `true`.
// That register can alternatively be one of EDN's that holds commands that EDN
// passes to CSRNG, in which case the `check_completion` argument must be
// `false`.
OT_WARN_UNUSED_RESULT
static status_t csrng_send_app_cmd(uint32_t reg_address,
                                   entropy_csrng_cmd_t cmd,
                                   bool check_completion) {
  enum {
    // This is to maintain full compliance with NIST SP 800-90A, which requires
    // the max generate output to be constrained to gen < 2 ^ 12 bits or 0x800
    // 128-bit blocks.
    kMaxGenerateSizeIn128BitBlocks = 0x800,
  };
  if (cmd.generate_len > kMaxGenerateSizeIn128BitBlocks) {
    return OUT_OF_RANGE();
  }

  HARDENED_TRY(csrng_fsm_idle_wait());

  uint32_t reg;
  bool cmd_ready;
  do {
    reg = abs_mmio_read32(kBaseCsrng + CSRNG_SW_CMD_STS_REG_OFFSET);
    cmd_ready = bitfield_bit32_read(reg, CSRNG_SW_CMD_STS_CMD_RDY_BIT);
  } while (!cmd_ready);

#define ENTROPY_CMD(m, i) ((bitfield_field32_t){.mask = m, .index = i})
  // The application command header is not specified as a register in the
  // hardware specification, so the fields are mapped here by hand. The
  // command register also accepts arbitrary 32bit data.
  static const bitfield_field32_t kAppCmdFieldFlag0 = ENTROPY_CMD(0xf, 8);
  static const bitfield_field32_t kAppCmdFieldCmdId = ENTROPY_CMD(0xf, 0);
  static const bitfield_field32_t kAppCmdFieldCmdLen = ENTROPY_CMD(0xf, 4);
  static const bitfield_field32_t kAppCmdFieldGlen = ENTROPY_CMD(0x7ffff, 12);
#undef ENTROPY_CMD

  uint32_t cmd_len = cmd.seed_material == NULL ? 0 : cmd.seed_material->len;

  if (cmd_len & ~kAppCmdFieldCmdLen.mask) {
    return OTCRYPTO_RECOV_ERR;
  }

  // TODO: Consider removing this since the driver will be constructing these
  // commands internally.
  // Ensure the `seed_material` array is word-aligned, so it can be loaded to a
  // CPU register with natively aligned loads.
  if (cmd.seed_material != NULL &&
      misalignment32_of((uintptr_t)cmd.seed_material->data) != 0) {
    return OTCRYPTO_RECOV_ERR;
  }

  if (check_completion) {
    // Clear the `cs_cmd_req_done` bit, which is asserted whenever a command
    // request is completed, because that bit will be used below to determine if
    // this command request is completed.
    reg = bitfield_bit32_write(0, CSRNG_INTR_STATE_CS_CMD_REQ_DONE_BIT, true);
    abs_mmio_write32(kBaseCsrng + CSRNG_INTR_STATE_REG_OFFSET, reg);
  }

  // Build and write application command header.
  reg = bitfield_field32_write(0, kAppCmdFieldCmdId, cmd.id);
  reg = bitfield_field32_write(reg, kAppCmdFieldCmdLen, cmd_len);
  reg = bitfield_field32_write(reg, kAppCmdFieldGlen, cmd.generate_len);

  if (launder32(cmd.disable_trng_input) == kHardenedBoolTrue) {
    reg = bitfield_field32_write(reg, kAppCmdFieldFlag0, kMultiBitBool4True);
  }

  abs_mmio_write32(reg_address, reg);

  for (size_t i = 0; i < cmd_len; ++i) {
    abs_mmio_write32(reg_address, cmd.seed_material->data[i]);
  }

  if (check_completion) {
    if (cmd.id == kEntropyDrbgOpGenerate) {
      // The Generate command is complete only after all entropy bits have been
      // consumed. Thus poll the register that indicates if entropy bits are
      // available.
      do {
        reg = abs_mmio_read32(kBaseCsrng + CSRNG_GENBITS_VLD_REG_OFFSET);
      } while (!bitfield_bit32_read(reg, CSRNG_GENBITS_VLD_GENBITS_VLD_BIT));

    } else {
      // The non-Generate commands complete earlier, so poll the "command
      // request done" interrupt bit.  Once it is set, the "status" bit is
      // updated.
      do {
        reg = abs_mmio_read32(kBaseCsrng + CSRNG_INTR_STATE_REG_OFFSET);
      } while (!bitfield_bit32_read(reg, CSRNG_INTR_STATE_CS_CMD_REQ_DONE_BIT));

      // Check the "status" bit, which will be 1 only if there was an error.
      reg = abs_mmio_read32(kBaseCsrng + CSRNG_SW_CMD_STS_REG_OFFSET);
      if (bitfield_bit32_read(reg, CSRNG_SW_CMD_STS_CMD_STS_BIT)) {
        return OTCRYPTO_RECOV_ERR;
      }
    }
  }

  return OTCRYPTO_OK;
}

/**
 * Enables the CSRNG block with the SW application and internal state registers
 * enabled.
 */
static void csrng_configure(void) {
  uint32_t reg =
      bitfield_field32_write(0, CSRNG_CTRL_ENABLE_FIELD, kMultiBitBool4True);
  reg = bitfield_field32_write(reg, CSRNG_CTRL_SW_APP_ENABLE_FIELD,
                               kMultiBitBool4True);
  reg = bitfield_field32_write(reg, CSRNG_CTRL_READ_INT_STATE_FIELD,
                               kMultiBitBool4True);
  abs_mmio_write32(kBaseCsrng + CSRNG_CTRL_REG_OFFSET, reg);
}

/**
 * Stops a given EDN instance.
 *
 * It also resets the EDN CSRNG command buffer to avoid synchronization issues
 * with the upstream CSRNG instance.
 *
 * @param edn_address The based address of the target EDN block.
 */
static void edn_stop(uint32_t edn_address) {
  // FIFO clear is only honored if edn is enabled. This is needed to avoid
  // synchronization issues with the upstream CSRNG instance.
  uint32_t reg = abs_mmio_read32(edn_address + EDN_CTRL_REG_OFFSET);
  abs_mmio_write32(edn_address + EDN_CTRL_REG_OFFSET,
                   bitfield_field32_write(reg, EDN_CTRL_CMD_FIFO_RST_FIELD,
                                          kMultiBitBool4True));

  // Disable EDN and restore the FIFO clear at the same time so that no rogue
  // command can get in after the clear above.
  abs_mmio_write32(edn_address + EDN_CTRL_REG_OFFSET, EDN_CTRL_REG_RESVAL);
}

/**
 * Blocks until EDN instance is ready to execute a new CSNRG command.
 *
 * @param edn_address EDN base address.
 * @returns an error if the EDN error status bit is set.
 */
OT_WARN_UNUSED_RESULT
static status_t edn_ready_block(uint32_t edn_address) {
  uint32_t reg;
  do {
    reg = abs_mmio_read32(edn_address + EDN_SW_CMD_STS_REG_OFFSET);
  } while (!bitfield_bit32_read(reg, EDN_SW_CMD_STS_CMD_RDY_BIT));

  if (bitfield_bit32_read(reg, EDN_SW_CMD_STS_CMD_STS_BIT)) {
    return OTCRYPTO_RECOV_ERR;
  }
  return OTCRYPTO_OK;
}

/**
 * Configures EDN instance based on `config` options.
 *
 * @param config EDN configuration options.
 * @returns error on failure.
 */
OT_WARN_UNUSED_RESULT
static status_t edn_configure(const edn_config_t *config) {
  HARDENED_TRY(csrng_send_app_cmd(
      config->base_address + EDN_RESEED_CMD_REG_OFFSET, config->reseed, false));
  HARDENED_TRY(
      csrng_send_app_cmd(config->base_address + EDN_GENERATE_CMD_REG_OFFSET,
                         config->generate, false));
  abs_mmio_write32(
      config->base_address + EDN_MAX_NUM_REQS_BETWEEN_RESEEDS_REG_OFFSET,
      config->reseed_interval);

  uint32_t reg =
      bitfield_field32_write(0, EDN_CTRL_EDN_ENABLE_FIELD, kMultiBitBool4True);
  reg = bitfield_field32_write(reg, EDN_CTRL_AUTO_REQ_MODE_FIELD,
                               kMultiBitBool4True);
  abs_mmio_write32(config->base_address + EDN_CTRL_REG_OFFSET, reg);

  HARDENED_TRY(edn_ready_block(config->base_address));
  HARDENED_TRY(
      csrng_send_app_cmd(config->base_address + EDN_SW_CMD_REQ_REG_OFFSET,
                         config->instantiate, false));
  return edn_ready_block(config->base_address);
}

/**
 * Stops the current mode of operation and disables the entropy_src module.
 *
 * All configuration registers are set to their reset values to avoid
 * synchronization issues with internal FIFOs.
 */
static void entropy_src_stop(void) {
  abs_mmio_write32(kBaseEntropySrc + ENTROPY_SRC_MODULE_ENABLE_REG_OFFSET,
                   ENTROPY_SRC_MODULE_ENABLE_REG_RESVAL);

  // Set default values for other critical registers to avoid synchronization
  // issues.
  abs_mmio_write32(kBaseEntropySrc + ENTROPY_SRC_ENTROPY_CONTROL_REG_OFFSET,
                   ENTROPY_SRC_ENTROPY_CONTROL_REG_RESVAL);
  abs_mmio_write32(kBaseEntropySrc + ENTROPY_SRC_CONF_REG_OFFSET,
                   ENTROPY_SRC_CONF_REG_RESVAL);
  abs_mmio_write32(kBaseEntropySrc + ENTROPY_SRC_HEALTH_TEST_WINDOWS_REG_OFFSET,
                   ENTROPY_SRC_HEALTH_TEST_WINDOWS_REG_RESVAL);
  abs_mmio_write32(kBaseEntropySrc + ENTROPY_SRC_ALERT_THRESHOLD_REG_OFFSET,
                   ENTROPY_SRC_ALERT_THRESHOLD_REG_RESVAL);
}

/**
 * Disables the entropy complex.
 *
 * The order of operations is important to avoid synchronization issues across
 * blocks. For Example, EDN has FIFOs used to send commands to the downstream
 * CSRNG instances. Such FIFOs are not cleared when EDN is reconfigured, and an
 * explicit clear FIFO command needs to be set by software (see #14506). There
 * may be additional race conditions for downstream blocks that are
 * processing requests from an upstream endpoint (e.g. entropy_src processing a
 * request from CSRNG, or CSRNG processing a request from EDN). To avoid these
 * issues, it is recommended to first disable EDN, then CSRNG and entropy_src
 * last.
 *
 * See hw/ip/csrng/doc/_index.md#module-enable-and-disable for more details.
 */
static void entropy_complex_stop_all(void) {
  edn_stop(kBaseEdn0);
  edn_stop(kBaseEdn1);
  abs_mmio_write32(kBaseCsrng + CSRNG_CTRL_REG_OFFSET, CSRNG_CTRL_REG_RESVAL);
  entropy_src_stop();
}

/**
 * Set the value of an entropy_src threshold register.
 *
 * Only sets the FIPS threshold value, not the bypass threshold field; for the
 * bypass threshold we use the reset value, which is ignored if looser than the
 * thresholds already set.
 *
 * @param name Name of register (e.g. REPCNT, BUCKET).
 * @param value Value to set for the FIPS_THRESH field.
 */
#define SET_FIPS_THRESH(name, value)                                \
  abs_mmio_write32(                                                 \
      kBaseEntropySrc + ENTROPY_SRC_##name##_THRESHOLDS_REG_OFFSET, \
      bitfield_field32_write(                                       \
          ENTROPY_SRC_##name##_THRESHOLDS_REG_RESVAL,               \
          ENTROPY_SRC_##name##_THRESHOLDS_FIPS_THRESH_FIELD, value));

/**
 * Configures the entropy_src with based on `config` options.
 *
 * @param config Entropy Source configuration options.
 * @return error on failure.
 */
OT_WARN_UNUSED_RESULT
static status_t entropy_src_configure(const entropy_src_config_t *config) {
  if (config->bypass_conditioner != kMultiBitBool4False) {
    // Bypassing the conditioner is not supported.
    return OTCRYPTO_BAD_ARGS;
  }

  // Control register configuration.
  uint32_t reg = bitfield_field32_write(
      0, ENTROPY_SRC_ENTROPY_CONTROL_ES_ROUTE_FIELD, config->route_to_firmware);
  reg = bitfield_field32_write(reg, ENTROPY_SRC_ENTROPY_CONTROL_ES_TYPE_FIELD,
                               config->bypass_conditioner);
  abs_mmio_write32(kBaseEntropySrc + ENTROPY_SRC_ENTROPY_CONTROL_REG_OFFSET,
                   reg);

  // Config register configuration
  reg = bitfield_field32_write(0, ENTROPY_SRC_CONF_FIPS_ENABLE_FIELD,
                               config->fips_enable);
  reg = bitfield_field32_write(reg,
                               ENTROPY_SRC_CONF_ENTROPY_DATA_REG_ENABLE_FIELD,
                               config->route_to_firmware);
  reg = bitfield_field32_write(reg, ENTROPY_SRC_CONF_THRESHOLD_SCOPE_FIELD,
                               kMultiBitBool4False);
  reg = bitfield_field32_write(reg, ENTROPY_SRC_CONF_RNG_BIT_ENABLE_FIELD,
                               config->single_bit_mode);
  reg = bitfield_field32_write(reg, ENTROPY_SRC_CONF_RNG_BIT_SEL_FIELD, 0);
  abs_mmio_write32(kBaseEntropySrc + ENTROPY_SRC_CONF_REG_OFFSET, reg);

  // Configure health test window. Conditioning bypass is not supported.
  abs_mmio_write32(
      kBaseEntropySrc + ENTROPY_SRC_HEALTH_TEST_WINDOWS_REG_OFFSET,
      bitfield_field32_write(ENTROPY_SRC_HEALTH_TEST_WINDOWS_REG_RESVAL,
                             ENTROPY_SRC_HEALTH_TEST_WINDOWS_FIPS_WINDOW_FIELD,
                             config->fips_test_window_size));

  // Configure alert threshold
  reg = bitfield_field32_write(
      0, ENTROPY_SRC_ALERT_THRESHOLD_ALERT_THRESHOLD_FIELD,
      config->alert_threshold);
  reg = bitfield_field32_write(
      reg, ENTROPY_SRC_ALERT_THRESHOLD_ALERT_THRESHOLD_INV_FIELD,
      ~config->alert_threshold);
  abs_mmio_write32(kBaseEntropySrc + ENTROPY_SRC_ALERT_THRESHOLD_REG_OFFSET,
                   reg);

  // Configure health test thresholds. Conditioning bypass is not supported.
  SET_FIPS_THRESH(REPCNT, config->repcnt_threshold);
  SET_FIPS_THRESH(REPCNTS, config->repcnts_threshold);
  SET_FIPS_THRESH(ADAPTP_HI, config->adaptp_hi_threshold);
  SET_FIPS_THRESH(ADAPTP_LO, config->adaptp_lo_threshold);
  SET_FIPS_THRESH(BUCKET, config->bucket_threshold);
  SET_FIPS_THRESH(MARKOV_HI, config->markov_hi_threshold);
  SET_FIPS_THRESH(MARKOV_LO, config->markov_lo_threshold);
  SET_FIPS_THRESH(EXTHT_HI, config->extht_hi_threshold);
  SET_FIPS_THRESH(EXTHT_LO, config->extht_lo_threshold);

  // Enable entropy_src.
  abs_mmio_write32(kBaseEntropySrc + ENTROPY_SRC_MODULE_ENABLE_REG_OFFSET,
                   kMultiBitBool4True);

  // TODO: Add FI checks.
  return OTCRYPTO_OK;
}

/**
 * Verify the value of an entropy_src threshold register.
 *
 * Only checks the FIPS threshold value, not the bypass threshold field.
 *
 * @param name Name of register (e.g. REPCNT, BUCKET).
 * @param exp Expected value of the FIPS_THRESH field.
 */
#define VERIFY_FIPS_THRESH(name, exp)                                  \
  do {                                                                 \
    uint32_t reg = abs_mmio_read32(                                    \
        kBaseEntropySrc + ENTROPY_SRC_##name##_THRESHOLDS_REG_OFFSET); \
    uint32_t act = bitfield_field32_read(                              \
        reg, ENTROPY_SRC_##name##_THRESHOLDS_FIPS_THRESH_FIELD);       \
    if (act != exp) {                                                  \
      return OTCRYPTO_RECOV_ERR;                                       \
    }                                                                  \
  } while (false);

/**
 * Check the entropy_src configuration.
 *
 * Verifies that the entropy_src block is enabled and running in a
 * FIPS-compatible mode that forwards results to hardware. Checks the threshold
 * register values against the configuration provided.
 *
 * @param config Entropy Source configuration options.
 * @return error on failure.
 */
OT_WARN_UNUSED_RESULT
static status_t entropy_src_check(const entropy_src_config_t *config) {
  if (config->fips_enable != kMultiBitBool4True ||
      config->bypass_conditioner != kMultiBitBool4False ||
      config->route_to_firmware != kMultiBitBool4False) {
    // This check only supports FIPS-compatible configurations which do not
    // bypass the conditioner or route to firmware.
    return OTCRYPTO_BAD_ARGS;
  }

  // Check that entropy_src is enabled.
  uint32_t reg =
      abs_mmio_read32(kBaseEntropySrc + ENTROPY_SRC_MODULE_ENABLE_REG_OFFSET);
  if (reg != kMultiBitBool4True) {
    return OTCRYPTO_RECOV_ERR;
  }

  // Check that entropy_src is running in a FIPS-enabled mode without bypassing
  // the conditioner (es_type) and while making results available to hardware
  // (es_route):
  //   1. CONF.FIPS_ENABLE = true
  //   2. CONF.RNG_BIT_ENABLE = false
  //   3. CONTROL.ES_TYPE = false
  //   3. CONTROL.ES_ROUTE = false
  reg = abs_mmio_read32(kBaseEntropySrc + ENTROPY_SRC_CONF_REG_OFFSET);
  uint32_t conf_fips_enable =
      bitfield_field32_read(reg, ENTROPY_SRC_CONF_FIPS_ENABLE_FIELD);
  uint32_t conf_rng_bit_enable =
      bitfield_field32_read(reg, ENTROPY_SRC_CONF_RNG_BIT_ENABLE_FIELD);
  if (conf_fips_enable != kMultiBitBool4True ||
      conf_rng_bit_enable != kMultiBitBool4False) {
    return OTCRYPTO_RECOV_ERR;
  }
  reg =
      abs_mmio_read32(kBaseEntropySrc + ENTROPY_SRC_ENTROPY_CONTROL_REG_OFFSET);
  uint32_t control_es_type =
      bitfield_field32_read(reg, ENTROPY_SRC_ENTROPY_CONTROL_ES_TYPE_FIELD);
  uint32_t control_es_route =
      bitfield_field32_read(reg, ENTROPY_SRC_ENTROPY_CONTROL_ES_ROUTE_FIELD);
  if (control_es_type != kMultiBitBool4False ||
      control_es_route != kMultiBitBool4False) {
    return OTCRYPTO_RECOV_ERR;
  }

  // Check health test window register.
  reg = abs_mmio_read32(kBaseEntropySrc +
                        ENTROPY_SRC_HEALTH_TEST_WINDOWS_REG_OFFSET);
  if (bitfield_field32_read(
          reg, ENTROPY_SRC_HEALTH_TEST_WINDOWS_FIPS_WINDOW_FIELD) !=
      config->fips_test_window_size) {
    return OTCRYPTO_RECOV_ERR;
  }

  // Check alert threshold.
  uint32_t exp_reg = bitfield_field32_write(
      0, ENTROPY_SRC_ALERT_THRESHOLD_ALERT_THRESHOLD_FIELD,
      config->alert_threshold);
  exp_reg = bitfield_field32_write(
      exp_reg, ENTROPY_SRC_ALERT_THRESHOLD_ALERT_THRESHOLD_INV_FIELD,
      ~config->alert_threshold);
  if (exp_reg != abs_mmio_read32(kBaseEntropySrc +
                                 ENTROPY_SRC_ALERT_THRESHOLD_REG_OFFSET)) {
    return OTCRYPTO_RECOV_ERR;
  }

  // Check health test thresholds.
  VERIFY_FIPS_THRESH(REPCNT, config->repcnt_threshold);
  VERIFY_FIPS_THRESH(REPCNTS, config->repcnts_threshold);
  VERIFY_FIPS_THRESH(ADAPTP_HI, config->adaptp_hi_threshold);
  VERIFY_FIPS_THRESH(ADAPTP_LO, config->adaptp_lo_threshold);
  VERIFY_FIPS_THRESH(BUCKET, config->bucket_threshold);
  VERIFY_FIPS_THRESH(MARKOV_HI, config->markov_hi_threshold);
  VERIFY_FIPS_THRESH(MARKOV_LO, config->markov_lo_threshold);
  VERIFY_FIPS_THRESH(EXTHT_HI, config->extht_hi_threshold);
  VERIFY_FIPS_THRESH(EXTHT_LO, config->extht_lo_threshold);

  // TODO: more FI checks on comparisons here.
  return OTCRYPTO_OK;
}

/**
 * Check the CSRNG configuration.
 *
 * This check simply ensures that the CSRNG is enabled.
 *
 * @param config EDN configuration.
 * @return error on failure.
 */
OT_WARN_UNUSED_RESULT
static status_t csrng_check(void) {
  uint32_t reg = abs_mmio_read32(kBaseCsrng + CSRNG_CTRL_REG_OFFSET);
  uint32_t enable = bitfield_field32_read(reg, CSRNG_CTRL_ENABLE_FIELD);
  if (enable == kMultiBitBool4True) {
    return OTCRYPTO_OK;
  }
  return OTCRYPTO_RECOV_ERR;
}

/**
 * Check the EDN configuration.
 *
 * This check simply ensures that the EDN is enabled and running in auto_req
 * mode.
 *
 * @param config EDN configuration.
 * @return error on failure.
 */
OT_WARN_UNUSED_RESULT
static status_t edn_check(const edn_config_t *config) {
  uint32_t reg = abs_mmio_read32(config->base_address + EDN_CTRL_REG_OFFSET);
  uint32_t edn_enable = bitfield_field32_read(reg, EDN_CTRL_EDN_ENABLE_FIELD);
  uint32_t auto_req_mode =
      bitfield_field32_read(reg, EDN_CTRL_AUTO_REQ_MODE_FIELD);
  if (edn_enable == kMultiBitBool4True && auto_req_mode == kMultiBitBool4True) {
    return OTCRYPTO_OK;
  }
  return OTCRYPTO_RECOV_ERR;
}

status_t entropy_complex_init(void) {
  entropy_complex_stop_all();

  const entropy_complex_config_t *config =
      &kEntropyComplexConfigs[kEntropyComplexConfigIdContinuous];
  if (launder32(config->id) != kEntropyComplexConfigIdContinuous) {
    return OTCRYPTO_RECOV_ERR;
  }

  HARDENED_TRY(entropy_src_configure(&config->entropy_src));
  csrng_configure();
  HARDENED_TRY(edn_configure(&config->edn0));
  return edn_configure(&config->edn1);
}

status_t entropy_complex_check(void) {
  const entropy_complex_config_t *config =
      &kEntropyComplexConfigs[kEntropyComplexConfigIdContinuous];
  if (launder32(config->id) != kEntropyComplexConfigIdContinuous) {
    return OTCRYPTO_RECOV_ERR;
  }

  HARDENED_TRY(entropy_src_check(&config->entropy_src));
  HARDENED_TRY(csrng_check());
  HARDENED_TRY(edn_check(&config->edn0));
  return edn_check(&config->edn1);
}

status_t entropy_csrng_instantiate(
    hardened_bool_t disable_trng_input,
    const entropy_seed_material_t *seed_material) {
  return csrng_send_app_cmd(kBaseCsrng + CSRNG_CMD_REQ_REG_OFFSET,
                            (entropy_csrng_cmd_t){
                                .id = kEntropyDrbgOpInstantiate,
                                .disable_trng_input = disable_trng_input,
                                .seed_material = seed_material,
                                .generate_len = 0,
                            },
                            true);
}

status_t entropy_csrng_reseed(hardened_bool_t disable_trng_input,
                              const entropy_seed_material_t *seed_material) {
  return csrng_send_app_cmd(kBaseCsrng + CSRNG_CMD_REQ_REG_OFFSET,
                            (entropy_csrng_cmd_t){
                                .id = kEntropyDrbgOpReseed,
                                .disable_trng_input = disable_trng_input,
                                .seed_material = seed_material,
                                .generate_len = 0,
                            },
                            true);
}

status_t entropy_csrng_update(const entropy_seed_material_t *seed_material) {
  return csrng_send_app_cmd(kBaseCsrng + CSRNG_CMD_REQ_REG_OFFSET,
                            (entropy_csrng_cmd_t){
                                .id = kEntropyDrbgOpUpdate,
                                .seed_material = seed_material,
                                .generate_len = 0,
                            },
                            true);
}

status_t entropy_csrng_generate_start(
    const entropy_seed_material_t *seed_material, size_t len) {
  // Round up the number of 128bit blocks. Aligning with respect to uint32_t.
  // TODO(#6112): Consider using a canonical reference for alignment operations.
  const uint32_t num_128bit_blocks = ceil_div(len, 4);
  return csrng_send_app_cmd(kBaseCsrng + CSRNG_CMD_REQ_REG_OFFSET,
                            (entropy_csrng_cmd_t){
                                .id = kEntropyDrbgOpGenerate,
                                .seed_material = seed_material,
                                .generate_len = num_128bit_blocks,
                            },
                            true);
}

status_t entropy_csrng_generate_data_get(uint32_t *buf, size_t len,
                                         hardened_bool_t fips_check) {
  static_assert(kEntropyCsrngBitsBufferNumWords == 4,
                "kEntropyCsrngBitsBufferNumWords must be 4.");
  size_t nblocks = ceil_div(len, 4);
  status_t res = OTCRYPTO_OK;
  for (size_t block_idx = 0; block_idx < nblocks; ++block_idx) {
    // Block until there is more data available in the genbits buffer. CSRNG
    // generates data in 128bit chunks (i.e. 4 words).
    uint32_t reg;
    do {
      reg = abs_mmio_read32(kBaseCsrng + CSRNG_GENBITS_VLD_REG_OFFSET);
    } while (!bitfield_bit32_read(reg, CSRNG_GENBITS_VLD_GENBITS_VLD_BIT));

    if (fips_check != kHardenedBoolFalse &&
        !bitfield_bit32_read(reg, CSRNG_GENBITS_VLD_GENBITS_FIPS_BIT)) {
      // Entropy isn't FIPS-compatible, so we should return an error when
      // done. However, we still need to read the result to clear CSRNG's FIFO.
      res = OTCRYPTO_RECOV_ERR;
    }

    // Read the full 128-bit block, in reverse word order to match known-answer
    // tests. To clear the FIFO, we need to read all blocks generated by the
    // request even if we don't use them.
    for (size_t offset = 0; offset < kEntropyCsrngBitsBufferNumWords;
         ++offset) {
      uint32_t word = abs_mmio_read32(kBaseCsrng + CSRNG_GENBITS_REG_OFFSET);
      size_t word_idx = (block_idx * kEntropyCsrngBitsBufferNumWords) +
                        kEntropyCsrngBitsBufferNumWords - 1 - offset;
      if (word_idx < len) {
        buf[word_idx] = word;
      }
    }
  }

  return res;
}

status_t entropy_csrng_generate(const entropy_seed_material_t *seed_material,
                                uint32_t *buf, size_t len,
                                hardened_bool_t fips_check) {
  HARDENED_TRY(entropy_csrng_generate_start(seed_material, len));
  return entropy_csrng_generate_data_get(buf, len, fips_check);
}

status_t entropy_csrng_uninstantiate(void) {
  return csrng_send_app_cmd(kBaseCsrng + CSRNG_CMD_REQ_REG_OFFSET,
                            (entropy_csrng_cmd_t){
                                .id = kEntropyDrbgOpUninstantiate,
                                .seed_material = NULL,
                                .generate_len = 0,
                            },
                            true);
}
