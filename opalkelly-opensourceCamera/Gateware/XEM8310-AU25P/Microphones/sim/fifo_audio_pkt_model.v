// Modele de simulation du FIFO a horloges independantes cree par add_mics.tcl (fifo_generator, non FWFT).
module fifo_audio_pkt (
    input  wire        rst, wr_clk, rd_clk,
    input  wire [31:0] din, input wire wr_en, input wire rd_en,
    output reg  [31:0] dout, output wire full, output wire empty,
    output wire [12:0] wr_data_count, output wire [12:0] rd_data_count
);
    reg [31:0] mem [0:4095];
    integer wp = 0, rp = 0;
    wire [12:0] cnt = wp - rp;
    assign full = (cnt >= 4095), empty = (cnt == 0);
    assign wr_data_count = cnt, rd_data_count = cnt;
    always @(posedge wr_clk) begin
        if (rst) wp <= 0; else if (wr_en && !full) begin mem[wp % 4096] <= din; wp <= wp + 1; end
    end
    always @(posedge rd_clk) begin
        if (rst) rp <= 0; else if (rd_en && !empty) begin dout <= mem[rp % 4096]; rp <= rp + 1; end
    end
endmodule
