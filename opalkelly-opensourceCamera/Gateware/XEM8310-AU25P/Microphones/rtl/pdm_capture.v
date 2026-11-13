// Capture des 32 lignes PDM (2 micros par ligne) : un bit par micro et par periode d'horloge.
// Canal 2k   = micro « haut » (SELECT = VDD) de la ligne k, canal 2k+1 = micro « bas ».
// INVERT_CLK = 1 si la carte inverse l'horloge (SN74LVC1GU04) : les roles des fronts sont echanges.
// L'attribution canal <-> micro physique (MK1/MK2 de chaque carte) est a confirmer sur la carte.
module pdm_capture #(
    parameter LINES      = 32,
    parameter INVERT_CLK = 0
) (
    input  wire                clk,
    input  wire [LINES-1:0]    dat_pin,      // lignes MicData[k] (deja dans le domaine clk, cf. IOB)
    input  wire                smp_hi,
    input  wire                smp_lo,
    input  wire                ce,
    output reg  [2*LINES-1:0]  pdm,
    output reg                 pdm_ce
);
    (* IOB = "TRUE" *) reg [LINES-1:0] dat_q;
    reg [LINES-1:0] hi_q, lo_q;
    wire s_hi = INVERT_CLK ? smp_lo : smp_hi;
    wire s_lo = INVERT_CLK ? smp_hi : smp_lo;
    integer k;
    always @(posedge clk) begin
        dat_q <= dat_pin;
        if (s_hi) hi_q <= dat_q;
        if (s_lo) lo_q <= dat_q;
        pdm_ce <= ce;
        if (ce) for (k = 0; k < LINES; k = k + 1) begin
            pdm[2*k]   <= hi_q[k];
            pdm[2*k+1] <= lo_q[k];
        end
    end
endmodule
