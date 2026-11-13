`timescale 1ns/1ps
// Banc du FIR : injecte des sorties CIC (cic_in.hex, 64 x 19 bits par mot) toutes les 240 cycles (96 MHz).
module tb_fir;
    parameter N = 300;
    localparam CH = 64, WI = 19, WO = 24;
    reg clk = 0; always #5.2083 clk = ~clk;
    reg rst = 1;
    reg [CH*WI-1:0] mem [0:N-1];
    initial $readmemh("cic_in.hex", mem);
    reg [CH*WI-1:0] din; reg dv = 0;
    wire [CH*WO-1:0] dout; wire ov;
    fir_bank #(.CH(CH), .LANES(8)) dut (.clk(clk), .rst(rst), .in(din), .in_valid(dv), .out(dout), .out_valid(ov));
    integer n = 0, c, f, nout = 0, cyc = 0;
    initial begin f = $fopen("fir_out.txt", "w"); #100 rst = 0; end
    always @(posedge clk) begin
        dv <= 1'b0;
        if (!rst) begin
            cyc <= cyc + 1;
            if (cyc % 240 == 0 && n < N) begin din <= mem[n]; dv <= 1'b1; n <= n + 1; end
        end
        if (ov) begin
            for (c = 0; c < CH; c = c + 1) $fwrite(f, "%0d ", $signed(dout[c*WO +: WO]));
            $fwrite(f, "\n"); nout = nout + 1;
        end
        if (n == N && cyc > N*240 + 600) begin $fclose(f); $display("OK: %0d sorties", nout); $finish; end
    end
endmodule
