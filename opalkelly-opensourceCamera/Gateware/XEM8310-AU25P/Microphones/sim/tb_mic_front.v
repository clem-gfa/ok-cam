`timescale 1ns/1ps
// Banc : 64 micros modelises (SPH0641LU4H-1, timings de la datasheet) -> mic_clkgen -> pdm_capture -> cic_bank.
// Ecrit pdm_dump.txt (mots PDM captures) et cic_out.txt (sorties CIC) pour verification par check_chain.py.
module tb_mic_front;
    parameter L = 3000;               // periodes PDM de la source
    parameter SD = 7;                 // delai d'echantillonnage (cycles)
    parameter INVERT = 0;             // 1 : horloge inversee sur la carte
    localparam LINES = 32, CH = 64, W = 19;

    reg clk = 0;
    always #5.2083 clk = ~clk;        // 96 MHz
    reg rst = 1, enable = 0;
    wire mic_clk_core, smp_hi, smp_lo, ce, ultra, run;

    mic_clkgen #(.STD_PERIODS(20), .SETTLE_PERIODS(10)) u_clk (
        .clk(clk), .rst(rst), .enable(enable), .sample_delay(SD[7:0]),
        .mic_clk(mic_clk_core), .smp_hi(smp_hi), .smp_lo(smp_lo), .ce(ce), .ultra(ultra), .run(run));

    // horloge vue par les micros (une inversion eventuelle sur la carte) + 3 ns de propagation
    wire pad_clk = INVERT ? ~mic_clk_core : mic_clk_core;
    wire mclk = pad_clk;

    // source : un mot de 64 bits par periode PDM
    reg [CH-1:0] src [0:L-1];
    initial $readmemh("pdm_src.hex", src);

    // micros
    tri0 [LINES-1:0] dat;             // la ligne est tiree a 0 quand aucun micro ne la pilote
    integer pn [0:CH-1];              // numero de periode de chaque micro
    reg [CH-1:0] drv_en;
    reg [CH-1:0] drv_val;
    genvar m;
    generate for (m = 0; m < CH; m = m + 1) begin : mic
        localparam HI = (m % 2 == 0);
        integer n;
        initial begin n = 0; drv_en[m] = 0; drv_val[m] = 0; end
        // micro haut : donnee sur front montant, relachee apres le front descendant
        // micro bas  : donnee sur front descendant, relachee apres le front montant
        if (HI) begin
            always @(posedge mclk) if (run_seen) begin drv_val[m] <= #28 src[n % L][m]; drv_en[m] <= #28 1'b1; n = n + 1; end
            always @(negedge mclk) drv_en[m] <= #10 1'b0;
        end else begin
            always @(negedge mclk) if (run_seen) begin drv_val[m] <= #28 src[(n) % L][m]; drv_en[m] <= #28 1'b1; n = n + 1; end
            always @(posedge mclk) drv_en[m] <= #10 1'b0;
        end
        assign dat[m/2] = drv_en[m] ? drv_val[m] : 1'bz;
    end endgenerate

    // les micros commencent a numeroter les bits quand la sequence est en regime etabli
    reg run_seen = 0;
    always @(posedge clk) if (run) run_seen <= 1;

    wire [CH-1:0] pdm; wire pdm_ce;
    pdm_capture #(.INVERT_CLK(INVERT)) u_cap (.clk(clk), .dat_pin(dat), .smp_hi(smp_hi), .smp_lo(smp_lo),
                                               .ce(ce), .pdm(pdm), .pdm_ce(pdm_ce));
    wire [CH*W-1:0] cic; wire cic_valid;
    cic_bank #(.CH(CH), .N(5), .R(12), .W(W)) u_cic (.clk(clk), .rst(rst), .pdm(pdm), .pdm_ce(pdm_ce),
                                                     .out(cic), .out_valid(cic_valid));

    integer f1, f2, k, ndump = 0, nout = 0;
    initial begin
        f1 = $fopen("pdm_dump.txt", "w"); f2 = $fopen("cic_out.txt", "w");
        #100 rst = 0; enable = 1;
    end
    always @(posedge clk) begin
        if (pdm_ce) begin $fdisplay(f1, "%h", pdm); ndump = ndump + 1; end
        if (cic_valid) begin
            for (k = 0; k < CH; k = k + 1) $fwrite(f2, "%0d ", $signed(cic[k*W +: W]));
            $fwrite(f2, "\n"); nout = nout + 1;
        end
        if (ndump == L - 40) begin
            $fclose(f1); $fclose(f2);
            $display("OK: %0d mots PDM, %0d sorties CIC, ultra=%b", ndump, nout, ultra);
            $finish;
        end
    end
endmodule
