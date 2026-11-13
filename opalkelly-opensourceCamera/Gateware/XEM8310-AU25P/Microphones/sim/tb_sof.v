`timescale 1ns/1ps
module tb_sof;
    reg clk = 0; always #5 clk = ~clk;
    reg v = 0, r = 0, u = 0; wire sof;
    axis_sof_monitor dut (.aclk(clk), .mon_tvalid(v), .mon_tready(r), .mon_tuser(u), .sof(sof));
    integer n = 0, pulses = 0, expected = 0, cyc = 0;
    // 5 trames de 16 pixels, ready/valid aleatoires ; tuser = 1 sur le 1er pixel de la trame
    integer px = 0;
    always @(posedge clk) begin
        cyc <= cyc + 1;
        if (sof) pulses <= pulses + 1;
        v <= ($random & 3) != 0; r <= ($random & 3) != 0;
        u <= (px % 16 == 0);
        if (v && r) begin if (u) expected <= expected + 1; px <= px + 1; end
        if (cyc == 400) begin
            // decale de un cycle : sof est registre
            $display("pulses=%0d attendus=%0d", pulses, expected);
            if (pulses == expected && expected > 5) $display("OK"); else $display("ECHEC");
            $finish;
        end
    end
endmodule
