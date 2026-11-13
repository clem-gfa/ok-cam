// FIR de decimation (x2) pour CH canaux, partage entre LANES voies (une voie = 1 multiplieur).
//   - filtre a phase lineaire de 73 coefficients (37 uniques, Q17), calcul replie : (x[n-k] + x[n-72+k]) * h[k]
//   - 400 kS/s (sortie du CIC, W_IN bits) -> 200 kS/s, sortie 24 bits = accumulateur >> 12
//   - historique en RAM circulaire de 128 mots par canal (deux copies : deux lectures par cycle)
// Budget : CPL = CH/LANES canaux par voie, 37 + 5 cycles par canal ; il faut CPL*42 < 2*(cycles entre deux in_valid).
// Avec 8 voies x 8 canaux : 336 cycles < 480 (96 MHz, in_valid tous les 240 cycles).
// La sortie de la 1re voie fixe la latence ; out_valid pulse quand les LANES voies ont termine.
module fir_bank #(
    parameter CH    = 64,
    parameter LANES = 8,
    parameter W_IN  = 19,
    parameter W_OUT = 24
) (
    input  wire                clk,
    input  wire                rst,
    input  wire [CH*W_IN-1:0]  in,        // tous les canaux, valide a in_valid
    input  wire                in_valid,
    output wire [CH*W_OUT-1:0] out,
    output wire                out_valid
);
    localparam CPL = CH / LANES;
    reg [CH*W_IN-1:0] hold;
    reg  ev;            // un cycle apres in_valid : hold est stable
    reg  par;           // parite de l'evenement : 1 = on calcule une sortie (decimation par 2)
    reg  ev_calc;
    always @(posedge clk) begin
        ev <= in_valid;
        if (rst) par <= 1'b0;
        if (in_valid) begin hold <= in; end
        ev_calc <= 1'b0;
        if (in_valid) begin par <= ~par; end
        if (in_valid && !par) ev_calc <= 1'b1;   // 1er, 3e... evenement : sortie y[n] avec n pair
    end

    wire [LANES-1:0] done;
    genvar l;
    generate for (l = 0; l < LANES; l = l + 1) begin : lane
        fir_lane #(.CPL(CPL), .W_IN(W_IN), .W_OUT(W_OUT)) u (
            .clk(clk), .rst(rst), .ev(ev), .ev_calc(ev_calc),
            .x(hold[l*CPL*W_IN +: CPL*W_IN]),
            .y(out[l*CPL*W_OUT +: CPL*W_OUT]),
            .done(done[l]));
    end endgenerate

    // out_valid : une impulsion quand toutes les voies ont fini (elles finissent au meme cycle)
    reg [LANES-1:0] done_seen;
    reg ov;
    always @(posedge clk) begin
        ov <= 1'b0;
        if (rst) done_seen <= 0;
        else begin
            done_seen <= done_seen | done;
            if (&(done_seen | done)) begin ov <= 1'b1; done_seen <= 0; end
        end
    end
    assign out_valid = ov;
endmodule

module fir_lane #(
    parameter CPL   = 8,
    parameter W_IN  = 19,
    parameter W_OUT = 24
) (
    input  wire                clk,
    input  wire                rst,
    input  wire                ev,
    input  wire                ev_calc,
    input  wire [CPL*W_IN-1:0] x,
    output reg  [CPL*W_OUT-1:0] y,
    output reg                 done
);
    `include "fir_coeffs.vh"
    localparam CB = (CPL > 1) ? $clog2(CPL) : 1;

    // ---- historique : deux copies, 128 mots par canal --------------------------------------
    reg signed [W_IN-1:0] mem_a [0:CPL*128-1];
    reg signed [W_IN-1:0] mem_b [0:CPL*128-1];
    reg [6:0] wptr [0:CPL-1];
    reg [6:0] cp   [0:CPL-1];
    integer i;
    initial for (i = 0; i < CPL; i = i + 1) begin wptr[i] = 0; cp[i] = 0; end
    initial for (i = 0; i < CPL*128; i = i + 1) begin mem_a[i] = 0; mem_b[i] = 0; end

    // ---- phase d'ecriture : un canal par cycle apres ev ------------------------------------
    reg        wbusy, wcalc;
    reg [CB:0] wc;
    wire [6:0] wnew = wptr[wc[CB-1:0]] + 7'd1;
    reg        start_calc;
    always @(posedge clk) begin
        start_calc <= 1'b0;
        if (rst) begin wbusy <= 1'b0; end
        else if (ev && !wbusy) begin wbusy <= 1'b1; wc <= 0; wcalc <= ev_calc; end
        else if (wbusy) begin
            mem_a[{wc[CB-1:0], wnew}] <= x[wc[CB-1:0]*W_IN +: W_IN];
            mem_b[{wc[CB-1:0], wnew}] <= x[wc[CB-1:0]*W_IN +: W_IN];
            wptr[wc[CB-1:0]] <= wnew;
            if (wcalc) cp[wc[CB-1:0]] <= wnew;
            wc <= wc + 1;
            if (wc == CPL - 1) begin wbusy <= 1'b0; start_calc <= wcalc; end
        end
    end

    // ---- phase de calcul : un canal apres l'autre, 37 coefficients chacun ---------------------
    reg        cbusy;
    reg [CB:0] cc;          // canal en cours
    reg [6:0]  k;           // coefficient en cours (emission des adresses)
    reg [6:0]  kv;
    reg        issue;
    reg [6:0]  ad_a, ad_b;
    reg signed [W_IN-1:0] rd_a, rd_b;
    reg        s1v, s1c; reg [5:0] s1k;
    reg signed [W_IN:0]   pre;
    reg signed [17:0]     cf;
    reg        s2v, s2first, s2last;
    reg signed [38:0]     prod;
    reg        s3v, s3first, s3last;
    reg signed [47:0]     acc;
    reg        s4last; reg [CB:0] s4ch;
    reg [CB:0] s1ch, s2ch, s3ch;
    reg        s1first, s1last;

    wire [CB-1:0] ccb = cc[CB-1:0];
    always @(posedge clk) begin
        done <= 1'b0;
        if (rst) begin cbusy <= 1'b0; issue <= 1'b0; s1v <= 0; s2v <= 0; s3v <= 0; end
        else begin
            // emission des adresses (etage 0)
            s1v <= 1'b0;
            if (start_calc) begin cbusy <= 1'b1; cc <= 0; k <= 0; issue <= 1'b1; end
            else if (issue) begin
                rd_a <= mem_a[{ccb, cp[ccb] - k}];
                rd_b <= mem_b[{ccb, cp[ccb] - 7'd72 + k}];
                s1v <= 1'b1; s1k <= k[5:0]; s1ch <= cc; s1first <= (k == 0); s1last <= (k == 36);
                if (k == 36) begin
                    k <= 0;
                    if (cc == CPL - 1) issue <= 1'b0; else cc <= cc + 1;
                end else k <= k + 1;
            end
            // etage 1 : pre-addition + lecture du coefficient
            s2v <= s1v; s2first <= s1first; s2last <= s1last; s2ch <= s1ch;
            if (s1v) begin
                pre <= (s1k == 6'd36) ? {rd_a[W_IN-1], rd_a} : ({rd_a[W_IN-1], rd_a} + {rd_b[W_IN-1], rd_b});
                cf  <= fir_coef(s1k);
            end
            // etage 2 : multiplication
            s3v <= s2v; s3first <= s2first; s3last <= s2last; s3ch <= s2ch;
            if (s2v) prod <= pre * cf;
            // etage 3 : accumulation, sortie en fin de canal
            s4last <= 1'b0;
            if (s3v) begin
                if (s3first) acc <= prod; else acc <= acc + prod;
                if (s3last) begin
                    y[s3ch[CB-1:0]*W_OUT +: W_OUT] <= (s3first ? prod : acc + prod) >>> 12;
                    s4last <= 1'b1; s4ch <= s3ch;
                    if (s3ch == CPL - 1) done <= 1'b1;
                end
            end
        end
    end
endmodule
