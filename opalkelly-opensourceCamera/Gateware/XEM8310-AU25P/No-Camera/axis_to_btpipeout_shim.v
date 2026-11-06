/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

module axis_to_btpipeout_shim (
    input  wire        aclk,
    input  wire        aresetn,

    // AXI-Stream slave
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis_video TDATA" *)
    input  wire [31:0] s_axis_video_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis_video TREADY" *)
    output wire        s_axis_video_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis_video TVALID" *)
    input  wire        s_axis_video_tvalid,

    // BT PipeOut
    (* X_INTERFACE_INFO = "opalkelly.com:interface:btpipeout:1.0 btpipeout_video EP_DATAIN" *)
    output wire [31:0] btpo_ep_datain_video,
    (* X_INTERFACE_INFO = "opalkelly.com:interface:btpipeout:1.0 btpipeout_video EP_READ" *)
    input  wire        btpo_ep_read_video,
    (* X_INTERFACE_INFO = "opalkelly.com:interface:btpipeout:1.0 btpipeout_video EP_BLOCKSTROBE" *)
    input  wire        btpo_ep_blockstrobe_video,
    (* X_INTERFACE_INFO = "opalkelly.com:interface:btpipeout:1.0 btpipeout_video EP_READY" *)
    output wire        btpo_ep_ready_video
);

wire srst = ~aresetn;
wire full;

assign s_axis_video_tready = !full;
wire wr_en = s_axis_video_tready & s_axis_video_tvalid;

fifo_axis_to_btpipeout_shim fifo_inst (
    .clk(aclk),
    .srst(srst),

    // Write interface (from AXI-Stream)
    .din(s_axis_video_tdata),
    .wr_en(wr_en),
    .full(full),

    // Read interface (to BT PipeOut)
    .rd_en(btpo_ep_read_video),
    .dout(btpo_ep_datain_video),
    .prog_full(btpo_ep_ready_video)
);

endmodule
