`timescale 1ns/1ps
// Test de audio_ddr_ring : paquets ecrits dans le FIFO d'entree (horloge micros), anneau en « DDR4 » (modele AXI avec delais
// aleatoires, horloge 300 MHz), FIFO de sortie lu par un « PC » (horloge okClk) qui s'arrete pendant un long moment.
module tb_ddr_ring;
    parameter NPK = 120, STALL_A = 15, STALL_B = 70, RING_AW = 4;   // anneau de 16 paquets
    reg clk = 0;  always #5.0021 clk = ~clk;      // micros 99.96 MHz
    reg uclk = 0; always #1.6667 uclk = ~uclk;    // horloge utilisateur de la DDR4 (300 MHz)
    reg okclk = 0; always #4.9603 okclk = ~okclk; // 100.8 MHz
    reg aresetn = 0; reg rst_req = 0;
    initial begin #200 aresetn = 1; end

    // FIFO d'entree
    wire [127:0] stg_dout; wire stg_empty, stg_full; wire [10:0] stg_wcnt; wire [8:0] stg_rcnt; wire stg_rd_en;
    reg [31:0] stg_din; reg stg_wr_en = 0;
    fifo_audio_stage u_stg (.rst(1'b0), .wr_clk(clk), .rd_clk(uclk), .din(stg_din), .wr_en(stg_wr_en), .rd_en(stg_rd_en),
                            .dout(stg_dout), .full(stg_full), .empty(stg_empty), .wr_data_count(stg_wcnt), .rd_data_count(stg_rcnt));
    // FIFO de sortie
    wire [127:0] out_din; wire out_wr_en, out_full, out_empty; wire [10:0] out_wcnt; wire [12:0] out_rcnt; wire [31:0] out_dout; reg out_rd_en = 0;
    fifo_audio_out u_out (.rst(1'b0), .wr_clk(uclk), .rd_clk(okclk), .din(out_din), .wr_en(out_wr_en), .rd_en(out_rd_en),
                          .dout(out_dout), .full(out_full), .empty(out_empty), .wr_data_count(out_wcnt), .rd_data_count(out_rcnt));
    // anneau + memoire
    wire [31:0] awaddr, araddr; wire [7:0] awlen, arlen; wire [2:0] awsize, arsize, awprot, arprot; wire [1:0] awburst, arburst;
    wire awlock, arlock, awvalid, awready, wlast, wvalid, wready, bvalid, bready, arvalid, arready, rlast, rvalid, rready;
    wire [3:0] awcache, awqos, arcache, arqos; wire [127:0] wdata, rdata; wire [15:0] wstrb; wire [1:0] bresp, rresp;
    wire idle, space_ok; wire [15:0] used, peak, axierr;
    audio_ddr_ring #(.RING_BASE(32'hA0000000), .RING_AW(RING_AW)) dut (
        .clk(uclk), .aresetn(aresetn), .rst_req(rst_req),
        .stg_dout(stg_dout), .stg_empty(stg_empty), .stg_avail(stg_rcnt), .stg_rd_en(stg_rd_en),
        .out_din(out_din), .out_wr_en(out_wr_en), .out_full(out_full), .out_wr_count(out_wcnt),
        .m_axi_awaddr(awaddr), .m_axi_awlen(awlen), .m_axi_awsize(awsize), .m_axi_awburst(awburst), .m_axi_awlock(awlock),
        .m_axi_awcache(awcache), .m_axi_awprot(awprot), .m_axi_awqos(awqos), .m_axi_awvalid(awvalid), .m_axi_awready(awready),
        .m_axi_wdata(wdata), .m_axi_wstrb(wstrb), .m_axi_wlast(wlast), .m_axi_wvalid(wvalid), .m_axi_wready(wready),
        .m_axi_bresp(bresp), .m_axi_bvalid(bvalid), .m_axi_bready(bready),
        .m_axi_araddr(araddr), .m_axi_arlen(arlen), .m_axi_arsize(arsize), .m_axi_arburst(arburst), .m_axi_arlock(arlock),
        .m_axi_arcache(arcache), .m_axi_arprot(arprot), .m_axi_arqos(arqos), .m_axi_arvalid(arvalid), .m_axi_arready(arready),
        .m_axi_rdata(rdata), .m_axi_rresp(rresp), .m_axi_rlast(rlast), .m_axi_rvalid(rvalid), .m_axi_rready(rready),
        .idle(idle), .space_ok(space_ok), .used_pkts(used), .peak_pkts(peak), .axi_errors(axierr));
    axi_mem_model #(.BASE(32'hA0000000), .WORDS(16*256*2)) mem (
        .clk(uclk), .aresetn(aresetn),
        .awaddr(awaddr), .awlen(awlen), .awvalid(awvalid), .awready(awready), .wdata(wdata), .wlast(wlast), .wvalid(wvalid), .wready(wready),
        .bresp(bresp), .bvalid(bvalid), .bready(bready),
        .araddr(araddr), .arlen(arlen), .arvalid(arvalid), .arready(arready), .rdata(rdata), .rresp(rresp), .rlast(rlast), .rvalid(rvalid), .rready(rready));

    // producteur : un paquet de 1024 mots tous les 3000 cycles ; abandonne le paquet si pas de place (comme le paquetiseur)
    reg [1:0] sp = 0; always @(posedge clk) sp <= {sp[0], space_ok};   // synchronisation vers l'horloge micros
    integer pk = 0, cycle = 0, i = 0, accepted = 0, dropped = 0, sending = 0;
    reg [31:0] accepted_ids [0:NPK-1];
    always @(posedge clk) begin
        stg_wr_en <= 0;
        if (aresetn) begin
            cycle <= cycle + 1;
            if (!sending && cycle % 3000 == 100 && pk < NPK) begin
                if (sp[1] && (stg_wcnt < 1024)) begin sending <= 1; i <= 0; accepted_ids[accepted] <= pk; accepted <= accepted + 1; end
                else dropped <= dropped + 1;
                pk <= pk + 1;
            end
            if (sending) begin
                stg_din <= (pk - 1) * 1024 + i; stg_wr_en <= 1; i <= i + 1;
                if (i == 1023) sending <= 0;
            end
        end
    end

    // consommateur « PC » : lit le FIFO de sortie, s'arrete entre les paquets STALL_A et STALL_B
    reg rd_d = 0; integer got = 0, errs = 0, k = 0; reg [31:0] expect_w;
    wire stalled = (pk >= STALL_A && pk < STALL_B);
    always @(posedge okclk) begin
        out_rd_en <= 0; rd_d <= out_rd_en;
        if (!stalled && !out_empty && !(out_rd_en)) out_rd_en <= 1;
        if (rd_d) begin
            // le mot lu appartient au paquet accepte numero got/1024
            expect_w = accepted_ids[got / 1024] * 1024 + (got % 1024);
            if (out_dout !== expect_w) begin errs = errs + 1; if (errs < 5) $display("ERREUR mot %0d : %h attendu %h", got, out_dout, expect_w); end
            got = got + 1;
        end
        if (pk >= NPK && cycle > NPK * 3000 + 30000) begin
            $display("paquets : acceptes=%0d abandonnes=%0d ; mots lus=%0d (=%0d paquets) ; erreurs de donnees=%0d ; pic de l'anneau=%0d ; erreurs AXI modele=%0d, DUT=%0d ; idle=%b",
                     accepted, dropped, got, got / 1024, errs, peak, mem.errors, axierr, idle);
            if (errs == 0 && got == accepted * 1024 && dropped > 0 && peak >= 14 && mem.errors == 0 && axierr == 0) $display("OK");
            else $display("ECHEC");
            $finish;
        end
    end
endmodule
