`timescale 1ns/1ps
// Banc du paquetiseur : instants synthetiques toutes les 480 cycles (96 MHz), FIFO aval de 4096 mots dont la
// lecture est suspendue pendant un moment (le PC ne suit plus) -> des paquets doivent etre abandonnes entiers.
module tb_audio_pkt;
    parameter NPK     = 45;          // paquets de la source
    parameter STALL_A = 10, STALL_B = 22;   // lecture suspendue du paquet STALL_A au paquet STALL_B
    localparam CH = 64, W = 24, NS = 21, DEPTH = 4096;

    reg clk = 0; always #5.2083 clk = ~clk;
    reg vclk = 0; always #5.0020 vclk = ~vclk;     // 99.96 MHz
    reg rst = 1;
    reg run = 0;

    // SOF video : une impulsion toutes les 50000 periodes de vclk
    reg vsof = 0; integer vc = 0;
    always @(posedge vclk) begin vc <= vc + 1; vsof <= (vc % 50000 == 49999); end

    wire [63:0] ts, vts; wire [31:0] vcnt;
    ts_unit u_ts (.clk(clk), .rst(rst), .vclk(vclk), .vsof(vsof), .ts(ts), .vts(vts), .vcnt(vcnt));

    // source : mot w de l'instant g = g*48 + w
    reg [CH*W-1:0] din; reg dv = 0;
    integer g = 0, cyc = 0, w;

    // FIFO aval
    reg [31:0] fifo [0:DEPTH-1];
    integer wp = 0, rp = 0, cnt = 0;
    wire [31:0] tdata; wire tvalid, tlast; wire tready = (cnt < DEPTH);
    wire space_ok = (DEPTH - cnt) >= 1024;
    wire [31:0] overruns, dropped;
    audio_packetizer #(.CH(CH), .W(W), .NS(NS)) dut (
        .clk(clk), .rst(rst), .run(run), .in(din), .in_valid(dv), .ts(ts), .vts(vts), .vcnt(vcnt),
        .space_ok(space_ok), .m_tdata(tdata), .m_tvalid(tvalid), .m_tlast(tlast), .m_tready(tready),
        .overruns(overruns), .dropped(dropped));

    integer f;
    wire draining = !(g >= STALL_A*NS && g < STALL_B*NS);
    initial begin f = $fopen("audio_words.txt", "w"); #100 rst = 0; run = 1; end
    always @(posedge clk) if (!rst) begin
        cyc <= cyc + 1;
        dv <= 1'b0;
        if (cyc % 480 == 0 && g < NPK*NS) begin
            for (w = 0; w < 48; w = w + 1) din[32*w +: 32] <= g*48 + w;
            dv <= 1'b1; g <= g + 1;
        end
        // ecriture dans le FIFO
        if (tvalid && tready) begin fifo[wp % DEPTH] <= tdata; wp <= wp + 1; end
        // lecture (le « PC »)
        if (draining && cnt > 0 && (cyc % 2 == 0)) begin
            $fdisplay(f, "%h", fifo[rp % DEPTH]); rp <= rp + 1;
        end
        cnt <= cnt + ((tvalid && tready) ? 1 : 0) - ((draining && cnt > 0 && (cyc % 2 == 0)) ? 1 : 0);
        if (g == NPK*NS && cyc > NPK*NS*480 + 6000) begin
            $fclose(f); $display("OK: %0d paquets source, abandonnes=%0d overruns=%0d", NPK, dropped, overruns); $finish;
        end
    end
endmodule
