// Base de temps commune audio/video.
//   ts   : compteur libre 64 bits (un tick par cycle de clk, clk = horloge audio, 96 MHz nominal)
//   vts  : valeur de ts au dernier debut de trame video (SOF), vcnt : nombre de SOF vus
// Le SOF vient d'un autre domaine d'horloge (vclk) : transfert par bascule + double registre, ~2-3 cycles
// de clk d'incertitude (quelques dizaines de ns).
module ts_unit (
    input  wire        clk,
    input  wire        rst,
    input  wire        vclk,
    input  wire        vsof,         // impulsion d'un cycle de vclk au debut de chaque trame
    output reg  [63:0] ts,
    output reg  [63:0] vts,
    output reg  [31:0] vcnt
);
    reg tog;
    always @(posedge vclk) if (vsof) tog <= ~tog;
    initial tog = 1'b0;

    reg s1, s2, s3;
    always @(posedge clk) begin
        s1 <= tog; s2 <= s1; s3 <= s2;
        if (rst) begin ts <= 64'd0; vts <= 64'd0; vcnt <= 32'd0; end
        else begin
            ts <= ts + 64'd1;
            if (s2 != s3) begin vts <= ts; vcnt <= vcnt + 32'd1; end
        end
    end
endmodule
