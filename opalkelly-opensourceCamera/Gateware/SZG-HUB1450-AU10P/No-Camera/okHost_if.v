//------------------------------------------------------------------------
// okHost_if.v
//
// A wrapper around okHost for use within the IPI block designer.
//
// Copyright (c) 2025 Opal Kelly Incorporated
//------------------------------------------------------------------------

`default_nettype none

module okHost_if (

    
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF s_axis:m_axis:m_axi:m_axil, ASSOCIATED_RESET axis_aresetn:axi_aresetn:axil_aresetn" *)
    output wire         aclk,

    input  wire [S_AXIS_TDATA_WIDTH-1:0]  s_axis_tdata,
    input  wire                           s_axis_tvalid,
    output wire                           s_axis_tready,

    output wire [M_AXIS_TDATA_WIDTH-1:0]  m_axis_tdata,
    output wire                           m_axis_tvalid,
    input  wire                           m_axis_tready,
    output wire                           m_axis_tlast,

    output wire [63:0]                    m_axi_awaddr,
    output wire [7:0]                     m_axi_awlen,
    output wire [2:0]                     m_axi_awsize,
    output wire [1:0]                     m_axi_awburst,
    output wire [3:0]                     m_axi_awcache,
    output wire [2:0]                     m_axi_awprot,
    output wire                           m_axi_awvalid,
    input  wire                           m_axi_awready,
    output wire [M_AXI_DATA_WIDTH-1:0]    m_axi_wdata,
    output wire                           m_axi_wlast,
    output wire                           m_axi_wvalid,
    input  wire                           m_axi_wready,
    input  wire [1:0]                     m_axi_bresp,
    input  wire                           m_axi_bvalid,
    output wire                           m_axi_bready,
    output wire [63:0]                    m_axi_araddr,
    output wire [7:0]                     m_axi_arlen,
    output wire [2:0]                     m_axi_arsize,
    output wire [1:0]                     m_axi_arburst,
    output wire [3:0]                     m_axi_arcache,
    output wire [2:0]                     m_axi_arprot,
    output wire                           m_axi_arvalid,
    input  wire                           m_axi_arready,
    input  wire [M_AXI_DATA_WIDTH-1:0]    m_axi_rdata,
    input  wire [1:0]                     m_axi_rresp,
    input  wire                           m_axi_rlast,
    input  wire                           m_axi_rvalid,
    output wire                           m_axi_rready,

    output wire [63:0]                    m_axil_awaddr,
    output wire [2:0]                     m_axil_awprot,
    output wire                           m_axil_awvalid,
    input  wire                           m_axil_awready,
    output wire [31:0]                    m_axil_wdata,
    output wire                           m_axil_wvalid,
    input  wire                           m_axil_wready,
    input  wire [1:0]                     m_axil_bresp,
    input  wire                           m_axil_bvalid,
    output wire                           m_axil_bready,
    output wire [63:0]                    m_axil_araddr,
    output wire [2:0]                     m_axil_arprot,
    output wire                           m_axil_arvalid,
    input  wire                           m_axil_arready,
    input  wire [31:0]                    m_axil_rdata,
    input  wire [1:0]                     m_axil_rresp,
    input  wire                           m_axil_rvalid,
    output wire                           m_axil_rready,

    output wire                           axis_aresetn,
    output wire                           axi_aresetn,
    output wire                           axil_aresetn,
    
    (* X_INTERFACE_IGNORE = "TRUE" *)
    inout  wire [OKUHU_WIDTH_BITS-1:0]    okUHU,
    (* X_INTERFACE_IGNORE = "TRUE" *)
    input  wire [OKUH_WIDTH_BITS-1:0]     okUH,
    (* X_INTERFACE_IGNORE = "TRUE" *)
    output wire [OKHU_WIDTH_BITS-1:0]     okHU
);

parameter MODE = 3;

parameter S_AXIS_TDATA_WIDTH =
    (MODE == 2) ? 128 :
    (MODE == 3) ? 64  :
    (MODE == 4) ? 8   : 64;

parameter M_AXIS_TDATA_WIDTH =
    (MODE == 2) ? 8   :
    (MODE == 3) ? 32  :
    (MODE == 4) ? 64  : 32;

parameter M_AXI_DATA_WIDTH =
    (MODE == 2) ? 128 :
    (MODE == 3) ? 64  :
    (MODE == 4) ? 64  : 64;

parameter OKUHU_WIDTH_BITS =
    (MODE == 2) ? 6 :
    (MODE == 3) ? 6 :
    (MODE == 4) ? 6 : 6;

parameter OKUH_WIDTH_BITS =
    (MODE == 2) ? 4  :
    (MODE == 3) ? 22 :
    (MODE == 4) ? 37 : 22;

parameter OKHU_WIDTH_BITS =
    (MODE == 2) ? 41 :
    (MODE == 3) ? 21 :
    (MODE == 4) ? 2  : 20;

parameter CLK_FREQUENCY_HZ =
    (MODE == 2) ? 156_250_000 :
    (MODE == 3) ? 156_250_000 :
    (MODE == 4) ? 125_000_000 : 160_000_000;


okHost okHost_i (
    .aclk                (aclk),

    .s_axis_tdata        (s_axis_tdata),
    .s_axis_tvalid       (s_axis_tvalid),
    .s_axis_tready       (s_axis_tready),

    .m_axis_tdata        (m_axis_tdata),
    .m_axis_tvalid       (m_axis_tvalid),
    .m_axis_tready       (m_axis_tready),
    .m_axis_tlast        (m_axis_tlast),

    .m_axi_awaddr        (m_axi_awaddr),
    .m_axi_awlen         (m_axi_awlen),
    .m_axi_awsize        (m_axi_awsize),
    .m_axi_awburst       (m_axi_awburst),
    .m_axi_awcache       (m_axi_awcache),
    .m_axi_awprot        (m_axi_awprot),
    .m_axi_awvalid       (m_axi_awvalid),
    .m_axi_awready       (m_axi_awready),

    .m_axi_wdata         (m_axi_wdata),
    .m_axi_wlast         (m_axi_wlast),
    .m_axi_wvalid        (m_axi_wvalid),
    .m_axi_wready        (m_axi_wready),

    .m_axi_bresp         (m_axi_bresp),
    .m_axi_bvalid        (m_axi_bvalid),
    .m_axi_bready        (m_axi_bready),

    .m_axi_araddr        (m_axi_araddr),
    .m_axi_arlen         (m_axi_arlen),
    .m_axi_arsize        (m_axi_arsize),
    .m_axi_arburst       (m_axi_arburst),
    .m_axi_arcache       (m_axi_arcache),
    .m_axi_arprot        (m_axi_arprot),
    .m_axi_arvalid       (m_axi_arvalid),
    .m_axi_arready       (m_axi_arready),

    .m_axi_rdata         (m_axi_rdata),
    .m_axi_rresp         (m_axi_rresp),
    .m_axi_rlast         (m_axi_rlast),
    .m_axi_rvalid        (m_axi_rvalid),
    .m_axi_rready        (m_axi_rready),

    .m_axil_awaddr       (m_axil_awaddr),
    .m_axil_awprot       (m_axil_awprot),
    .m_axil_awvalid      (m_axil_awvalid),
    .m_axil_awready      (m_axil_awready),

    .m_axil_wdata        (m_axil_wdata),
    .m_axil_wvalid       (m_axil_wvalid),
    .m_axil_wready       (m_axil_wready),

    .m_axil_bresp        (m_axil_bresp),
    .m_axil_bvalid       (m_axil_bvalid),
    .m_axil_bready       (m_axil_bready),

    .m_axil_araddr       (m_axil_araddr),
    .m_axil_arprot       (m_axil_arprot),
    .m_axil_arvalid      (m_axil_arvalid),
    .m_axil_arready      (m_axil_arready),

    .m_axil_rdata        (m_axil_rdata),
    .m_axil_rresp        (m_axil_rresp),
    .m_axil_rvalid       (m_axil_rvalid),
    .m_axil_rready       (m_axil_rready),

    .axis_aresetn        (axis_aresetn),
    .axi_aresetn         (axi_aresetn),
    .axil_aresetn        (axil_aresetn),

    .okUH                (okUH),
    .okHU                (okHU),
    .okUHU               (okUHU)
);

endmodule

`default_nettype wire
