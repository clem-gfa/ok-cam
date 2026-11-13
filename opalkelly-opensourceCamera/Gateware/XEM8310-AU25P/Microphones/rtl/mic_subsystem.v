// Sous-systeme micros pour l'integration dans le block design (module reference).
//   mic_clk_o / mic_data_i -> chaine PDM -> CIC -> FIR -> paquets -> FIFO -> pipe FrontPanel a blocs (EP 0xA1)
//   WireIn  0x01 : bit0 = enable, bit1 = reset, bits[15:8] = delai d'echantillonnage (cycles de clk)
//   WireOut 0x20 : bit0 = run, bit1 = ultra, bit2 = locked, bit3 = fifo plein vu (collant)
//   WireOut 0x21 : paquets abandonnes ;  WireOut 0x22 : overruns du paquetiseur
// Domaines : clk (horloge micros, ~95,5 MHz), ok_clk (okClk, registres FrontPanel et lecture du FIFO), vclk (video).
module mic_subsystem #(
    parameter DIV_STD        = 40,
    parameter DIV_ULTRA      = 20,
    parameter STD_PERIODS    = 48000,
    parameter SETTLE_PERIODS = 48000,
    parameter INVERT_CLK     = 0
) (
    input  wire        clk,
    input  wire        locked,
    input  wire        ok_clk,
    input  wire        vclk,
    input  wire        vsof,

    (* X_INTERFACE_INFO = "opalkelly.com:interface:wirein:1.0 wirein01_mic_ctrl EP_DATAOUT" *)
    input  wire [31:0] wi01_ep_dataout_mic_ctrl,
    (* X_INTERFACE_INFO = "opalkelly.com:interface:wireout:1.0 wireout20_mic_status EP_DATAIN" *)
    output wire [31:0] wo20_ep_datain_mic_status,
    (* X_INTERFACE_INFO = "opalkelly.com:interface:wireout:1.0 wireout21_mic_dropped EP_DATAIN" *)
    output wire [31:0] wo21_ep_datain_mic_dropped,
    (* X_INTERFACE_INFO = "opalkelly.com:interface:wireout:1.0 wireout22_mic_overruns EP_DATAIN" *)
    output wire [31:0] wo22_ep_datain_mic_overruns,

    (* X_INTERFACE_INFO = "opalkelly.com:interface:btpipeout:1.0 btpipeout_audio EP_DATAIN" *)
    output wire [31:0] btpo_ep_datain_audio,
    (* X_INTERFACE_INFO = "opalkelly.com:interface:btpipeout:1.0 btpipeout_audio EP_READ" *)
    input  wire        btpo_ep_read_audio,
    (* X_INTERFACE_INFO = "opalkelly.com:interface:btpipeout:1.0 btpipeout_audio EP_BLOCKSTROBE" *)
    input  wire        btpo_ep_blockstrobe_audio,
    (* X_INTERFACE_INFO = "opalkelly.com:interface:btpipeout:1.0 btpipeout_audio EP_READY" *)
    output wire        btpo_ep_ready_audio,

    output wire        mic_clk_o,
    input  wire [31:0] mic_data_i
);
    // ---- commandes (okClk -> clk, quasi statiques) ------------------------------------------
    (* ASYNC_REG = "TRUE" *) reg [15:0] c1, c2;
    always @(posedge clk) begin c1 <= wi01_ep_dataout_mic_ctrl[15:0]; c2 <= c1; end
    wire enable = c2[0];
    wire rst    = c2[1] | ~locked;
    wire [7:0] sd = c2[15:8];

    // ---- chaine ------------------------------------------------------------------------------
    wire smp_hi, smp_lo, ce, ultra, run;
    mic_clkgen #(.DIV_STD(DIV_STD), .DIV_ULTRA(DIV_ULTRA), .STD_PERIODS(STD_PERIODS), .SETTLE_PERIODS(SETTLE_PERIODS)) u_clk (
        .clk(clk), .rst(rst), .enable(enable), .sample_delay(sd),
        .mic_clk(mic_clk_o), .smp_hi(smp_hi), .smp_lo(smp_lo), .ce(ce), .ultra(ultra), .run(run));

    wire [63:0] pdm; wire pdm_ce;
    pdm_capture #(.INVERT_CLK(INVERT_CLK)) u_cap (.clk(clk), .dat_pin(mic_data_i), .smp_hi(smp_hi), .smp_lo(smp_lo),
                                                  .ce(ce), .pdm(pdm), .pdm_ce(pdm_ce));
    wire [64*19-1:0] cic; wire cic_v;
    cic_bank #(.CH(64), .N(5), .R(12), .W(19)) u_cic (.clk(clk), .rst(rst), .pdm(pdm), .pdm_ce(pdm_ce), .out(cic), .out_valid(cic_v));
    wire [64*24-1:0] fir; wire fir_v;
    fir_bank #(.CH(64), .LANES(8), .W_IN(19), .W_OUT(24)) u_fir (.clk(clk), .rst(rst), .in(cic), .in_valid(cic_v), .out(fir), .out_valid(fir_v));

    wire [63:0] ts, vts; wire [31:0] vcnt;
    ts_unit u_ts (.clk(clk), .rst(rst), .vclk(vclk), .vsof(vsof), .ts(ts), .vts(vts), .vcnt(vcnt));

    // ---- paquets -> FIFO ----------------------------------------------------------------------
    wire [31:0] tdata; wire tvalid, tlast, tready;
    wire [12:0] wr_cnt, rd_cnt;
    wire        full, empty;
    wire space_ok = (13'd4095 - wr_cnt) >= 13'd1024;       // FIFO de 4095 mots utiles
    wire [31:0] overruns, dropped;
    audio_packetizer #(.CH(64), .W(24), .NS(21)) u_pkt (
        .clk(clk), .rst(rst), .run(run), .in(fir), .in_valid(fir_v), .ts(ts), .vts(vts), .vcnt(vcnt),
        .space_ok(space_ok), .m_tdata(tdata), .m_tvalid(tvalid), .m_tlast(tlast), .m_tready(tready),
        .overruns(overruns), .dropped(dropped));
    assign tready = ~full;

    // FIFO a horloges independantes (IP Vivado cree par add_mics.tcl), lecture non FWFT comme le FIFO video.
    fifo_audio_pkt u_fifo (
        .rst(rst), .wr_clk(clk), .rd_clk(ok_clk),
        .din(tdata), .wr_en(tvalid & tready), .rd_en(btpo_ep_read_audio),
        .dout(btpo_ep_datain_audio), .full(full), .empty(empty),
        .wr_data_count(wr_cnt), .rd_data_count(rd_cnt));

    // EP_READY : un bloc complet (1024 mots = 4096 octets) est disponible
    reg rdy;
    always @(posedge ok_clk) rdy <= (rd_cnt >= 13'd1024);
    assign btpo_ep_ready_audio = rdy;

    // ---- statut (clk -> okClk) ---------------------------------------------------------------
    reg full_seen;
    always @(posedge clk) begin if (rst) full_seen <= 1'b0; else if (full) full_seen <= 1'b1; end
    (* ASYNC_REG = "TRUE" *) reg [3:0] s1, s2;
    always @(posedge ok_clk) begin s1 <= {full_seen, locked, ultra, run}; s2 <= s1; end
    assign wo20_ep_datain_mic_status = {28'd0, s2};

    // compteurs : code de Gray pour traverser les domaines
    function [31:0] bin2gray(input [31:0] b); bin2gray = b ^ (b >> 1); endfunction
    function [31:0] gray2bin(input [31:0] g); integer i; begin gray2bin[31] = g[31];
        for (i = 30; i >= 0; i = i - 1) gray2bin[i] = gray2bin[i+1] ^ g[i]; end endfunction
    reg [31:0] gd, go;
    always @(posedge clk) begin gd <= bin2gray(dropped); go <= bin2gray(overruns); end
    (* ASYNC_REG = "TRUE" *) reg [31:0] gd1, gd2, go1, go2;
    always @(posedge ok_clk) begin gd1 <= gd; gd2 <= gd1; go1 <= go; go2 <= go1; end
    assign wo21_ep_datain_mic_dropped  = gray2bin(gd2);
    assign wo22_ep_datain_mic_overruns = gray2bin(go2);
endmodule
