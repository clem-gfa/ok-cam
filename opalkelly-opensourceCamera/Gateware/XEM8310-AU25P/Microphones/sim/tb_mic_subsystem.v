`timescale 1ns/1ps
// Test de bout en bout : 64 micros modelises -> mic_subsystem -> pipe 0xA1 lue par un « PC » (blocs de 1024 mots).
module tb_mic_subsystem;
    parameter L = 3000, NPDM = 3500;
    reg clk = 0; always #5.2083 clk = ~clk;       // 96 MHz
    reg okclk = 0; always #4.9603 okclk = ~okclk; // 100.8 MHz
    reg vclk = 0; always #5.002 vclk = ~vclk;
    reg [31:0] ctrl = 32'h2;      // reset logiciel au depart
    wire [31:0] st, dropped, overruns, pipe_dout; wire ready;
    reg rd = 0;
    wire mic_clk; tri0 [31:0] dat;

    mic_subsystem #(.STD_PERIODS(20), .SETTLE_PERIODS(10), .BLOCK_WORDS(1024)) dut (
        .clk(clk), .locked(1'b1), .ok_clk(okclk), .vclk(vclk), .vsof(1'b0),
        .wi01_ep_dataout_mic_ctrl(ctrl), .wo20_ep_datain_mic_status(st),
        .wo21_ep_datain_mic_dropped(dropped), .wo22_ep_datain_mic_overruns(overruns),
        .btpo_ep_datain_audio(pipe_dout), .btpo_ep_read_audio(rd), .btpo_ep_blockstrobe_audio(1'b0), .btpo_ep_ready_audio(ready),
        .mic_clk_o(mic_clk), .mic_data_i(dat));

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
        if (!rd && ready && cnt == 0) begin rd <= 1'b1; cnt <= 1024; end
        else if (rd) begin cnt <= cnt - 1; if (cnt == 1) rd <= 1'b0; end
    end
    always @(posedge clk) if (dut.pdm_ce) begin $fdisplay(fb, "%h", dut.pdm); npdm = npdm + 1; end
    always @(posedge clk) if (npdm == NPDM) begin
        $fclose(fa); $fclose(fb);
        $display("OK: %0d mots lus, status=%h dropped=%0d overruns=%0d", nwords, st, dropped, overruns); $finish;
    end
endmodule
