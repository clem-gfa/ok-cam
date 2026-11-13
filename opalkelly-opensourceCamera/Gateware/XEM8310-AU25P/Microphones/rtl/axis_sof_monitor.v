// Detecteur de debut de trame video, en observateur (interface AXI-Stream en mode « monitor » :
// rien n'est insere dans le flux). Une impulsion d'un cycle de aclk au premier pixel de chaque trame
// (tuser[0] = start of frame, avec tvalid & tready).
module axis_sof_monitor (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 aclk CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF mon" *)
    input  wire       aclk,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 mon TVALID" *)
    (* X_INTERFACE_MODE = "monitor" *)
    input  wire       mon_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 mon TREADY" *)
    input  wire       mon_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 mon TUSER" *)
    input  wire [0:0] mon_tuser,

    output reg        sof
);
    always @(posedge aclk) sof <= mon_tvalid & mon_tready & mon_tuser[0];
endmodule
