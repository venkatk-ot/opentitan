// Copyright lowRISC contributors.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

class lc_ctrl_env extends cip_base_env #(
    .CFG_T              (lc_ctrl_env_cfg),
    .COV_T              (lc_ctrl_env_cov),
    .VIRTUAL_SEQUENCER_T(lc_ctrl_virtual_sequencer),
    .SCOREBOARD_T       (lc_ctrl_scoreboard)
  );
  `uvm_component_utils(lc_ctrl_env)

  push_pull_agent#(.HostDataWidth(OTP_PROG_HDATA_WIDTH), .DeviceDataWidth(OTP_PROG_DDATA_WIDTH))
                   m_otp_prog_pull_agent;
  push_pull_agent#(.HostDataWidth(lc_ctrl_pkg::LcTokenWidth)) m_otp_token_pull_agent;
  `uvm_component_new

  function void build_phase(uvm_phase phase);
    super.build_phase(phase);

    // config power manager pin
    if (!uvm_config_db#(pwr_lc_vif)::get(this, "", "pwr_lc_vif", cfg.pwr_lc_vif)) begin
      `uvm_fatal(get_full_name(), "failed to get pwr_lc_vif from uvm_config_db")
    end
    if (!uvm_config_db#(lc_ctrl_vif)::get(this, "", "lc_ctrl_vif", cfg.lc_ctrl_vif)) begin
      `uvm_fatal(`gfn, "failed to get lc_ctrl_vif from uvm_config_db")
    end

    m_otp_prog_pull_agent = push_pull_agent#(.HostDataWidth(OTP_PROG_HDATA_WIDTH),
        .DeviceDataWidth(OTP_PROG_DDATA_WIDTH))::type_id::create("m_otp_prog_pull_agent", this);
    uvm_config_db#(push_pull_agent_cfg#(.HostDataWidth(OTP_PROG_HDATA_WIDTH),
        .DeviceDataWidth(OTP_PROG_DDATA_WIDTH)))::set(this, "m_otp_prog_pull_agent", "cfg",
        cfg.m_otp_prog_pull_agent_cfg);

    m_otp_token_pull_agent = push_pull_agent#(.HostDataWidth(lc_ctrl_pkg::LcTokenWidth))::type_id::
        create("m_otp_token_pull_agent", this);
    uvm_config_db#(push_pull_agent_cfg#(.HostDataWidth(lc_ctrl_pkg::LcTokenWidth)))::set(this,
        "m_otp_token_pull_agent", "cfg", cfg.m_otp_token_pull_agent_cfg);
  endfunction

  function void connect_phase(uvm_phase phase);
    super.connect_phase(phase);
    virtual_sequencer.otp_prog_pull_sequencer_h = m_otp_prog_pull_agent.sequencer;
    virtual_sequencer.otp_token_pull_sequencer_h = m_otp_token_pull_agent.sequencer;
    if (cfg.en_scb) begin
      m_otp_prog_pull_agent.monitor.analysis_port.connect(
          scoreboard.otp_prog_fifo.analysis_export);
      m_otp_token_pull_agent.monitor.analysis_port.connect(
          scoreboard.otp_token_fifo.analysis_export);
    end
  endfunction

endclass
