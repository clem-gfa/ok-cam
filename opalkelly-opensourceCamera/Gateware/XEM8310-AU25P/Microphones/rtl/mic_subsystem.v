// Sous-systeme micros pour l'integration dans le block design (module reference).
//   mic_clk_o / mic_data_i -> chaine PDM -> CIC -> FIR -> paquets -> FIFO -> pipe FrontPanel a blocs (EP 0xA1)
//   WireIn  0x01 : bit0 = enable, bit1 = reset, bits[15:8] = delai d'echantillonnage (cycles de clk)
//   WireOut 0x20 : bit0 = run, bit1 = ultra, bit2 = locked, bit3 = fifo plein vu (collant)
//   WireOut 0x21 : paquets abandonnes ;  WireOut 0x22 : overruns du paquetiseur
//   WireOut 0x23 : occupation du FIFO audio (mots de 32 bits disponibles a la lecture)
//   WireOut 0x24 : {pic, courant} du nombre de paquets dans l'anneau DDR4 (0 si USE_DDR = 0)
// USE_DDR = 1 : les paquets passent par un anneau en DDR4 (audio_ddr_ring, domaine ui_clk de la DDR4) entre deux
// FIFO de changement de domaine : il absorbe les blocages de l'appli PC (64 Mio = ~1,8 s au lieu de ~3,6 ms).
// Domaines : clk (horloge micros = clk_out2 de clk_wiz_0, 99,96 MHz), ok_clk (okClk, registres FrontPanel et lecture du FIFO), vclk (video).
module mic_subsystem #(
    parameter DIV_STD        = 44,     // 99,96 MHz / 44 = 2,272 MHz (mode standard)
    parameter DIV_ULTRA      = 22,     // 99,96 MHz / 22 = 4,544 MHz (mode ultrason)
    parameter STD_PERIODS    = 48000,
    parameter SETTLE_PERIODS = 48000,
    parameter INVERT_CLK     = 0,
    parameter FIFO_AW        = 15,     // profondeur du FIFO audio = 2^FIFO_AW mots de 32 bits (15 -> 128 Kio, ~3,6 ms) ; doit valoir celle de l'IP fifo_audio_pkt
    parameter USE_DDR        = 0,
    parameter BLOCK_WORDS    = 256     // taille de bloc de la pipe (mots 32 bits) : doit valoir le bloc utilise par le PC (1024 octets = 256)
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
    (* X_INTERFACE_INFO = "opalkelly.com:interface:wireout:1.0 wireout23_mic_level EP_DATAIN" *)
    output wire [31:0] wo23_ep_datain_mic_level,
    (* X_INTERFACE_INFO = "opalkelly.com:interface:wireout:1.0 wireout24_mic_ring EP_DATAIN" *)
    output wire [31:0] wo24_ep_datain_mic_ring,

    (* X_INTERFACE_INFO = "opalkelly.com:interface:btpipeout:1.0 btpipeout_audio EP_DATAIN" *)
    output wire [31:0] btpo_ep_datain_audio,
    (* X_INTERFACE_INFO = "opalkelly.com:interface:btpipeout:1.0 btpipeout_audio EP_READ" *)
    input  wire        btpo_ep_read_audio,
    (* X_INTERFACE_INFO = "opalkelly.com:interface:btpipeout:1.0 btpipeout_audio EP_BLOCKSTROBE" *)
    input  wire        btpo_ep_blockstrobe_audio,
    (* X_INTERFACE_INFO = "opalkelly.com:interface:btpipeout:1.0 btpipeout_audio EP_READY" *)
    output wire        btpo_ep_ready_audio,

    output wire        mic_clk_o,
    input  wire [31:0] mic_data_i,

    // ---- horloge de la DDR4 (USE_DDR = 1) et liaison avec audio_ddr_ring (module separe, une seule horloge : le
    // ---- maitre AXI y est isole pour que Vivado l'associe sans ambiguite a l'horloge de la DDR4) ----
    input  wire         ui_clk,
    output wire [127:0] ring_stg_dout,
    output wire         ring_stg_empty,
    output wire [9:0]   ring_stg_avail,
    input  wire         ring_stg_rd_en,
    input  wire [127:0] ring_out_din,
    input  wire         ring_out_wr_en,
    output wire         ring_out_full,
    output wire [11:0]  ring_out_wr_count,
    output wire         ring_rst_req,
    input  wire         ring_idle,
    input  wire         ring_space_ok,
    input  wire [15:0]  ring_used,
    input  wire [15:0]  ring_peak
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

    // ---- paquets -> FIFO(s) -> pipe ------------------------------------------------------------
    wire [31:0] tdata; wire tvalid, tlast, tready;
    wire        space_ok;
    wire        full_flag;                       // pour le statut « FIFO plein vu »
    wire [31:0] overruns, dropped;
    audio_packetizer #(.CH(64), .W(24), .NS(21)) u_pkt (
        .clk(clk), .rst(rst), .run(run), .in(fir), .in_valid(fir_v), .ts(ts), .vts(vts), .vcnt(vcnt),
        .space_ok(space_ok), .m_tdata(tdata), .m_tvalid(tvalid), .m_tlast(tlast), .m_tready(tready),
        .overruns(overruns), .dropped(dropped));

    // gray pour traverser les domaines (declarations ici : utilisees par les deux variantes)
    function [31:0] bin2gray(input [31:0] b); bin2gray = b ^ (b >> 1); endfunction
    function [31:0] gray2bin(input [31:0] g); integer i; begin gray2bin[31] = g[31];
        for (i = 30; i >= 0; i = i - 1) gray2bin[i] = gray2bin[i+1] ^ g[i]; end endfunction

    generate if (USE_DDR == 0) begin : g_fifo
        // FIFO a horloges independantes (IP fifo_audio_pkt), lecture non FWFT comme le FIFO video.
        wire [FIFO_AW-1:0] wr_cnt, rd_cnt;
        wire full, empty;
        localparam [FIFO_AW:0] FIFO_MAX = (1 << FIFO_AW) - 1;       // mots utiles du FIFO
        wire [FIFO_AW:0] fifo_free = FIFO_MAX - wr_cnt;
        assign space_ok = fifo_free >= 1024;
        assign tready = ~full;
        assign full_flag = full;
        fifo_audio_pkt u_fifo (
            .rst(rst), .wr_clk(clk), .rd_clk(ok_clk),
            .din(tdata), .wr_en(tvalid & tready), .rd_en(btpo_ep_read_audio),
            .dout(btpo_ep_datain_audio), .full(full), .empty(empty),
            .wr_data_count(wr_cnt), .rd_data_count(rd_cnt));
        // EP_READY : un bloc complet (BLOCK_WORDS mots) est disponible
        reg rdy;
        always @(posedge ok_clk) rdy <= (rd_cnt >= BLOCK_WORDS);
        assign btpo_ep_ready_audio = rdy;
        assign wo23_ep_datain_mic_level = {{(32 - FIFO_AW){1'b0}}, rd_cnt};
        assign wo24_ep_datain_mic_ring = 32'd0;
        // pas d'acces DDR4
        assign ring_stg_dout = 128'd0; assign ring_stg_empty = 1'b1; assign ring_stg_avail = 10'd0;
        assign ring_out_full = 1'b0; assign ring_out_wr_count = 12'd0; assign ring_rst_req = 1'b0;
    end else begin : g_ddr
        // ---- reset : les FIFO ne sont vides que lorsque l'anneau est au repos (jamais en pleine rafale AXI) ----
        (* ASYNC_REG = "TRUE" *) reg [1:0] rq;
        always @(posedge ui_clk) rq <= {rq[0], rst};
        wire rst_req_ui = rq[1];
        wire ring_idle_w;
        (* ASYNC_REG = "TRUE" *) reg [1:0] idl;
        always @(posedge clk) idl <= {idl[0], ring_idle_w};
        wire fifo_rst_stage = rst & idl[1];                 // domaine clk (wr_clk du FIFO d'entree)


        // ---- FIFO d'entree : 32 bits (clk) -> 128 bits FWFT (ui_clk), 2048 mots ----
        wire [127:0] stg_dout; wire stg_empty, stg_full, stg_rd_en;
        wire [11:0] stg_wcnt; wire [9:0] stg_rcnt;
        assign tready = ~stg_full;
        fifo_audio_stage u_stage (
            .rst(fifo_rst_stage), .wr_clk(clk), .rd_clk(ui_clk),
            .din(tdata), .wr_en(tvalid & tready), .rd_en(stg_rd_en),
            .dout(stg_dout), .full(stg_full), .empty(stg_empty),
            .wr_data_count(stg_wcnt), .rd_data_count(stg_rcnt));

        // ---- liaison avec audio_ddr_ring (instance separee dans le block design) ----
        wire [127:0] out_din = ring_out_din; wire out_wr_en = ring_out_wr_en;
        wire out_full, out_empty; wire [11:0] out_wcnt; wire [13:0] out_rcnt;
        assign ring_stg_dout = stg_dout; assign ring_stg_empty = stg_empty; assign ring_stg_avail = stg_rcnt;
        assign ring_out_full = out_full; assign ring_out_wr_count = out_wcnt;
        assign ring_rst_req = rst_req_ui;
        assign stg_rd_en = ring_stg_rd_en;
        wire ring_space_ui = ring_space_ok; wire [15:0] used_ui = ring_used, peak_ui = ring_peak;
        assign ring_idle_w = ring_idle;

        // ---- FIFO de sortie : 128 bits (ui_clk) -> 32 bits (okClk), 2048 beats = 8192 mots ----
        wire fifo_rst_out = rst_req_ui & ring_idle_w;           // domaine ui_clk (wr_clk du FIFO de sortie)
        fifo_audio_out u_out (
            .rst(fifo_rst_out), .wr_clk(ui_clk), .rd_clk(ok_clk),
            .din(out_din), .wr_en(out_wr_en), .rd_en(btpo_ep_read_audio),
            .dout(btpo_ep_datain_audio), .full(out_full), .empty(out_empty),
            .wr_data_count(out_wcnt), .rd_data_count(out_rcnt));
        reg rdy;
        always @(posedge ok_clk) rdy <= (out_rcnt >= BLOCK_WORDS);
        assign btpo_ep_ready_audio = rdy;
        assign wo23_ep_datain_mic_level = {18'd0, out_rcnt};

        // ---- un paquet n'est ecrit que s'il y a de la place dans l'anneau ET dans le FIFO d'entree ----
        (* ASYNC_REG = "TRUE" *) reg [1:0] sp;
        always @(posedge clk) sp <= {sp[0], ring_space_ui};
        assign space_ok = sp[1] && (stg_wcnt < 12'd1024);
        assign full_flag = ~sp[1];                      // « anneau plein vu »

        // ---- statut de l'anneau (ui_clk -> okClk, en code de Gray) ----
        reg [31:0] gring;
        wire [31:0] gu = bin2gray({16'd0, used_ui});
        wire [31:0] gp = bin2gray({16'd0, peak_ui});
        always @(posedge ui_clk) gring <= {gp[15:0], gu[15:0]};
        (* ASYNC_REG = "TRUE" *) reg [31:0] gr1, gr2;
        always @(posedge ok_clk) begin gr1 <= gring; gr2 <= gr1; end
        wire [31:0] du = gray2bin({16'd0, gr2[15:0]});
        wire [31:0] dp = gray2bin({16'd0, gr2[31:16]});
        assign wo24_ep_datain_mic_ring = {dp[15:0], du[15:0]};
    end endgenerate

    // ---- statut (clk -> okClk) ---------------------------------------------------------------
    reg full_seen;
    always @(posedge clk) begin if (rst) full_seen <= 1'b0; else if (full_flag) full_seen <= 1'b1; end
    (* ASYNC_REG = "TRUE" *) reg [3:0] s1, s2;
    always @(posedge ok_clk) begin s1 <= {full_seen, locked, ultra, run}; s2 <= s1; end
    assign wo20_ep_datain_mic_status = {28'd0, s2};

    // compteurs : code de Gray pour traverser les domaines
    reg [31:0] gd, go;
    always @(posedge clk) begin gd <= bin2gray(dropped); go <= bin2gray(overruns); end
    (* ASYNC_REG = "TRUE" *) reg [31:0] gd1, gd2, go1, go2;
    always @(posedge ok_clk) begin gd1 <= gd; gd2 <= gd1; go1 <= go; go2 <= go1; end
    assign wo21_ep_datain_mic_dropped  = gray2bin(gd2);
    assign wo22_ep_datain_mic_overruns = gray2bin(go2);
endmodule
