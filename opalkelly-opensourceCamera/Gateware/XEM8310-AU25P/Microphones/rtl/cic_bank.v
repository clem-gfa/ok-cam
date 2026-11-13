// Banque de CIC (un par canal), arithmetique exacte (equivalente a cumsum^N puis diff^N).
// Entree : un bit PDM par canal a chaque pdm_ce (1 -> +1, 0 -> -1). Sortie : tous les R pdm_ce,
// valeur signee de W bits, gain DC = R^N.  Les etages sont evalues sequentiellement apres pdm_ce
// (1 cycle par etage), ce qui evite une chaine d'additionneurs en cascade.  Il faut N+N+2 cycles
// de clk entre deux pdm_ce.  W >= N*log2(R) + 1.
module cic_bank #(
    parameter CH = 64,
    parameter N  = 5,
    parameter R  = 12,
    parameter W  = 19
) (
    input  wire              clk,
    input  wire              rst,
    input  wire [CH-1:0]     pdm,
    input  wire              pdm_ce,
    output reg  [CH*W-1:0]   out,
    output reg               out_valid
);
    reg [3:0]  rcnt;
    reg [2*N+2:0] p;        // sequenceur one-hot
    reg        emit;        // cette sequence produit une sortie
    reg [CH-1:0] x;

    genvar c;
    generate for (c = 0; c < CH; c = c + 1) begin : g
        reg signed [W-1:0] i1, i2, i3, i4, i5, d1, d2, d3, d4, d5, t;
        always @(posedge clk) begin
            if (rst) begin
                i1 <= 0; i2 <= 0; i3 <= 0; i4 <= 0; i5 <= 0;
                d1 <= 0; d2 <= 0; d3 <= 0; d4 <= 0; d5 <= 0; t <= 0;
            end else begin
                if (p[0]) i1 <= i1 + (x[c] ? 1 : -1);
                if (p[1]) i2 <= i2 + i1;
                if (p[2]) i3 <= i3 + i2;
                if (p[3]) i4 <= i4 + i3;
                if (p[4]) i5 <= i5 + i4;
                if (emit) begin
                    if (p[5]) begin t <= i5 - d1; d1 <= i5; end
                    if (p[6]) begin t <= t  - d2; d2 <= t;  end
                    if (p[7]) begin t <= t  - d3; d3 <= t;  end
                    if (p[8]) begin t <= t  - d4; d4 <= t;  end
                    if (p[9]) begin t <= t  - d5; d5 <= t;  end
                    if (p[10]) out[c*W +: W] <= t;
                end
            end
        end
    end endgenerate

    always @(posedge clk) begin
        out_valid <= 1'b0;
        if (rst) begin
            rcnt <= 0; p <= 0; emit <= 1'b0;
        end else begin
            p <= {p[2*N+1:0], 1'b0};
            if (pdm_ce) begin
                x <= pdm;
                p[0] <= 1'b1;
                emit <= (rcnt == R - 1);
                rcnt <= (rcnt == R - 1) ? 4'd0 : rcnt + 4'd1;
            end
            if (p[10] && emit) begin out_valid <= 1'b1; end
        end
    end
endmodule
