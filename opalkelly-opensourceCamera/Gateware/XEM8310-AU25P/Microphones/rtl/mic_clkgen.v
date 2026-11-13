// Horloge PDM des SPH0641LU4H-1 + strobes d'echantillonnage.
//
// Sequence imposee par la datasheet : ne pas demarrer en mode ultrason.
//   OFF -> STD (DIV_STD, 2,4 MHz) pendant STD_PERIODS periodes
//       -> ULTRA (DIV_ULTRA, 4,8 MHz) : SETTLE_PERIODS periodes d'attente (mode-change 10 ms)
//       -> RUN : les strobes smp_hi / smp_lo / ce sont generes.
// Le diviseur ne change qu'en fin de periode, avec un rapport cyclique de 50 %.
//
// Chronologie dans une periode (cnt = 0..div-1, mic_clk = (cnt < div/2), apres 1 cycle de registre) :
//   cnt = SD            : echantillon du micro « haut » (SELECT = VDD, donnee sur front montant)
//   cnt = div/2 + SD    : echantillon du micro « bas »  (SELECT = GND, donnee sur front descendant)
//   cnt = div/2 + SD + 1: ce (un bit par micro et par periode)
// Contrainte : div/2 + SD + 1 < div.
module mic_clkgen #(
    parameter DIV_STD       = 40,     // 96 MHz / 40 = 2,4 MHz
    parameter DIV_ULTRA     = 20,     // 96 MHz / 20 = 4,8 MHz
    parameter STD_PERIODS   = 48000,  // ~20 ms a 2,4 MHz
    parameter SETTLE_PERIODS= 48000   // ~10 ms a 4,8 MHz
) (
    input  wire       clk,
    input  wire       rst,
    input  wire       enable,
    input  wire [7:0] sample_delay,   // SD, en cycles de clk apres le front
    (* IOB = "TRUE" *) output reg mic_clk,
    output reg        smp_hi,
    output reg        smp_lo,
    output reg        ce,
    output reg        ultra,          // horloge en mode ultrason
    output reg        run             // sequence terminee, flux valide
);
    localparam S_OFF = 2'd0, S_STD = 2'd1, S_SETTLE = 2'd2, S_RUN = 2'd3;
    reg [1:0]  state;
    reg [7:0]  cnt;
    reg [31:0] per;
    wire [7:0] div  = ultra ? DIV_ULTRA[7:0] : DIV_STD[7:0];
    wire [7:0] half = {1'b0, div[7:1]};
    wire       last = (cnt == div - 8'd1);

    always @(posedge clk) begin
        smp_hi <= 1'b0;
        smp_lo <= 1'b0;
        ce     <= 1'b0;
        if (rst || !enable) begin
            state <= S_OFF; cnt <= 8'd0; per <= 32'd0; ultra <= 1'b0; run <= 1'b0; mic_clk <= 1'b0;
        end else begin
            if (state == S_OFF) begin
                state <= S_STD; cnt <= 8'd0; per <= 32'd0;
            end else begin
                cnt     <= last ? 8'd0 : cnt + 8'd1;
                mic_clk <= (cnt < half);
                if (state == S_RUN) begin
                    smp_hi <= (cnt == sample_delay);
                    smp_lo <= (cnt == half + sample_delay);
                    ce     <= (cnt == half + sample_delay + 8'd1);
                end
                if (last) begin
                    per <= per + 32'd1;
                    if (state == S_STD && per == STD_PERIODS - 1) begin
                        state <= S_SETTLE; per <= 32'd0; ultra <= 1'b1;
                    end else if (state == S_SETTLE && per == SETTLE_PERIODS - 1) begin
                        state <= S_RUN; per <= 32'd0; run <= 1'b1;
                    end
                end
            end
        end
    end
endmodule
