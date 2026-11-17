`timescale 1ns/1ps
// Test de bout en bout : 64 micros modelises -> mic_subsystem -> pipe 0xA1 lue par un « PC » (blocs de 1024 mots).
module tb_mic_subsystem_ddr;
    parameter L = 3000, NPDM = 12600, STALL_UNTIL = 6300;   // ~25 paquets ; lecture suspendue pendant ~12 paquets
    reg clk = 0; always #5.2083 clk = ~clk;       // 96 MHz
    reg okclk = 0; always #4.9603 okclk = ~okclk; // 100.8 MHz
    reg vclk = 0; always #5.002 vclk = ~vclk;
    reg uclk = 0; always #1.6667 uclk = ~uclk;   // horloge utilisateur DDR4 300 MHz
    reg uaresetn = 0; initial begin #500 uaresetn = 1; end
    reg [31:0] ctrl = 32'h2;      // reset logiciel au depart
    wire [31:0] st, dropped, overruns, pipe_dout, level; wire ready;
    reg rd = 0;
    wire mic_clk; tri0 [31:0] dat;

    wire [31:0] awaddr, araddr; wire [7:0] awlen, arlen; wire awvalid, awready, wlast, wvalid, wready, bvalid, bready, arvalid, arready, rlast, rvalid, rready;
    wire [127:0] wdata, rdata; wire [1:0] bresp, rresp;
    wire [2:0] awsize, arsize, awprot, arprot; wire [1:0] awburst, arburst; wire awlock, arlock; wire [3:0] awcache, awqos, arcache, arqos; wire [15:0] wstrb;
    wire [31:0] ring;
    mic_subsystem #(.STD_PERIODS(20), .SETTLE_PERIODS(10), .BLOCK_WORDS(1024), .USE_DDR(1), .RING_AW(4)) dut (
        .clk(clk), .locked(1'b1), .ok_clk(okclk), .vclk(vclk), .vsof(1'b0),
        .wi01_ep_dataout_mic_ctrl(ctrl), .wo20_ep_datain_mic_status(st),
        .wo21_ep_datain_mic_dropped(dropped), .wo22_ep_datain_mic_overruns(overruns),
        .btpo_ep_datain_audio(pipe_dout), .btpo_ep_read_audio(rd), .btpo_ep_blockstrobe_audio(1'b0), .btpo_ep_ready_audio(ready), .wo23_ep_datain_mic_level(level),
        .mic_clk_o(mic_clk), .mic_data_i(dat), .wo24_ep_datain_mic_ring(ring),
        .ui_clk(uclk), .ui_aresetn(uaresetn),
        .m_axi_awaddr(awaddr), .m_axi_awlen(awlen), .m_axi_awsize(awsize), .m_axi_awburst(awburst), .m_axi_awlock(awlock), .m_axi_awcache(awcache),
        .m_axi_awprot(awprot), .m_axi_awqos(awqos), .m_axi_awvalid(awvalid), .m_axi_awready(awready),
        .m_axi_wdata(wdata), .m_axi_wstrb(wstrb), .m_axi_wlast(wlast), .m_axi_wvalid(wvalid), .m_axi_wready(wready),
        .m_axi_bresp(bresp), .m_axi_bvalid(bvalid), .m_axi_bready(bready),
        .m_axi_araddr(araddr), .m_axi_arlen(arlen), .m_axi_arsize(arsize), .m_axi_arburst(arburst), .m_axi_arlock(arlock), .m_axi_arcache(arcache),
        .m_axi_arprot(arprot), .m_axi_arqos(arqos), .m_axi_arvalid(arvalid), .m_axi_arready(arready),
        .m_axi_rdata(rdata), .m_axi_rresp(rresp), .m_axi_rlast(rlast), .m_axi_rvalid(rvalid), .m_axi_rready(rready));
    axi_mem_model #(.BASE(32'hA0000000), .WORDS(16*256*2)) mem (
        .clk(uclk), .aresetn(uaresetn), .awaddr(awaddr), .awlen(awlen), .awvalid(awvalid), .awready(awready), .wdata(wdata), .wlast(wlast), .wvalid(wvalid), .wready(wready),
        .bresp(bresp), .bvalid(bvalid), .bready(bready), .araddr(araddr), .arlen(arlen), .arvalid(arvalid), .arready(arready), .rdata(rdata), .rresp(rresp), .rlast(rlast), .rvalid(rvalid), .rready(rready));

    // micros (meme modele que tb_mic_front)
    reg [63:0] src [0:L-1];
    initial $readmemh("pdm_src.hex", src);
    reg [63:0] drv_en, drv_val;
    reg run_seen = 0;
    always @(posedge clk) if (dut.run) run_seen <= 1;
    genvar m;
    generate for (m = 0; m < 64; m = m + 1) begin : mic
        integer n;
        initial begin n = 0; drv_en[m] = 0; drv_val[m] = 0; end
        if (m % 2 == 0) begin
            always @(posedge mic_clk) if (run_seen) begin drv_val[m] <= #28 src[n % L][m]; drv_en[m] <= #28 1'b1; n = n + 1; end
            always @(negedge mic_clk) drv_en[m] <= #10 1'b0;
        end else begin
            always @(negedge mic_clk) if (run_seen) begin drv_val[m] <= #28 src[n % L][m]; drv_en[m] <= #28 1'b1; n = n + 1; end
            always @(posedge mic_clk) drv_en[m] <= #10 1'b0;
        end
        assign dat[m/2] = drv_en[m] ? drv_val[m] : 1'bz;
    end endgenerate

    // le « PC » : des qu'un bloc est pret, lit 1024 mots (non FWFT : donnee valide au cycle suivant)
    integer fa, fb, cnt = 0, npdm = 0, nwords = 0; reg rd_d = 0;
    initial begin
        fa = $fopen("pipe_words.txt", "w"); fb = $fopen("pdm_dump.txt", "w");
        #300 ctrl = {16'd0, 8'd7, 8'd1};   // enable=1, SD=7, reset relache
    end
    always @(posedge okclk) begin
        rd_d <= rd;
        if (rd_d) begin $fdisplay(fa, "%h", pipe_dout); nwords = nwords + 1; end
        if (!rd && ready && cnt == 0 && npdm >= STALL_UNTIL) begin rd <= 1'b1; cnt <= 1024; end
        else if (rd) begin cnt <= cnt - 1; if (cnt == 1) rd <= 1'b0; end
    end
    always @(posedge clk) if (dut.pdm_ce) begin $fdisplay(fb, "%h", dut.pdm); npdm = npdm + 1; end
    always @(posedge clk) if (npdm >= NPDM && dut.g_ddr.u_ring.used_pkts == 0 && dut.g_ddr.out_rcnt < 1024 && dut.g_ddr.stg_empty) begin
        $fclose(fa); $fclose(fb);
        $display("OK: %0d mots lus, status=%h dropped=%0d overruns=%0d, anneau (pic,courant)=(%0d,%0d), erreurs AXI modele=%0d DUT=%0d", nwords, st, dropped, overruns, ring[31:16], ring[15:0], mem.errors, dut.g_ddr.axi_err); $finish;
    end
endmodule
