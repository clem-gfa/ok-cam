// Modeles de simulation des FIFO a horloges independantes et largeurs asymetriques (fifo_generator) du chemin DDR4.

// 32 bits (ecriture, wr_clk) -> 128 bits (lecture FWFT, rd_clk). Profondeur d'ecriture : 2048 mots.
module fifo_audio_stage (
    input  wire        rst, wr_clk, rd_clk,
    input  wire [31:0] din, input wire wr_en, input wire rd_en,
    output wire [127:0] dout, output wire full, output wire empty,
    output wire [11:0] wr_data_count, output wire [9:0] rd_data_count
);
    reg [31:0] mem [0:2047];
    integer wp = 0, rp = 0;                        // en mots (rp : multiple de 4)
    wire [12:0] words = wp - rp;
    assign empty = (words < 4), full = (words >= 2048);
    assign wr_data_count = words;
    assign rd_data_count = (words >> 2);
    assign dout = {mem[(rp+3) % 2048], mem[(rp+2) % 2048], mem[(rp+1) % 2048], mem[rp % 2048]};
    always @(posedge wr_clk) begin
        if (rst) wp <= 0; else if (wr_en && !full) begin mem[wp % 2048] <= din; wp <= wp + 1; end
    end
    always @(posedge rd_clk) begin
        if (rst) rp <= 0; else if (rd_en && !empty) rp <= rp + 4;
    end
endmodule

// 128 bits (ecriture, wr_clk) -> 32 bits (lecture non FWFT, rd_clk). Profondeur d'ecriture : 2048 beats.
module fifo_audio_out (
    input  wire         rst, wr_clk, rd_clk,
    input  wire [127:0] din, input wire wr_en, input wire rd_en,
    output reg  [31:0]  dout, output wire full, output wire empty,
    output wire [11:0]  wr_data_count, output wire [13:0] rd_data_count
);
    reg [31:0] mem [0:8191];
    integer wp = 0, rp = 0;                        // en mots
    wire [13:0] words = wp - rp;
    assign empty = (words == 0), full = (words > 8192 - 4);
    assign wr_data_count = (words >> 2);
    assign rd_data_count = words[13:0];
    always @(posedge wr_clk) begin
        if (rst) wp <= 0;
        else if (wr_en && !full) begin
            mem[wp % 8192] <= din[31:0]; mem[(wp+1) % 8192] <= din[63:32];
            mem[(wp+2) % 8192] <= din[95:64]; mem[(wp+3) % 8192] <= din[127:96];
            wp <= wp + 4;
        end
    end
    always @(posedge rd_clk) begin
        if (rst) rp <= 0; else if (rd_en && !empty) begin dout <= mem[rp % 8192]; rp <= rp + 1; end
    end
endmodule

// Esclave AXI4 128 bits simple : une rafale d'ecriture et une de lecture a la fois, delais aleatoires.
module axi_mem_model #(parameter [31:0] BASE = 32'hA0000000, parameter WORDS = 8192, parameter SEED = 1) (
    input  wire clk, input wire aresetn,
    input  wire [31:0] awaddr, input wire [7:0] awlen, input wire awvalid, output reg awready,
    input  wire [127:0] wdata, input wire wlast, input wire wvalid, output reg wready,
    output reg [1:0] bresp, output reg bvalid, input wire bready,
    input  wire [31:0] araddr, input wire [7:0] arlen, input wire arvalid, output reg arready,
    output reg [127:0] rdata, output reg [1:0] rresp, output reg rlast, output reg rvalid, input wire rready
);
    reg [127:0] mem [0:WORDS-1];
    integer seed = SEED;
    reg [31:0] wa, ra; reg [8:0] wleft, rleft; reg wact, ract;
    integer errors = 0;
    function integer rnd; input integer n; begin rnd = {$random(seed)} % n; end endfunction
    initial begin awready = 0; wready = 0; bvalid = 0; bresp = 0; arready = 0; rvalid = 0; rlast = 0; rresp = 0; wact = 0; ract = 0; end
    always @(posedge clk) begin
        if (!aresetn) begin awready <= 0; wready <= 0; bvalid <= 0; arready <= 0; rvalid <= 0; wact <= 0; ract <= 0; end
        else begin
            // ---- ecriture
            awready <= 0;
            if (!wact && !bvalid && awvalid && !awready && rnd(3) == 0) begin
                awready <= 1; wa <= awaddr; wleft <= awlen + 1; wact <= 1;
                if ((awaddr & 32'hFFF) != 0 || awlen != 255) errors = errors + 1;   // 4 Kio alignes, rafale de 256
            end
            wready <= wact && (rnd(4) != 0);
            if (wact && wvalid && wready) begin
                mem[((wa - BASE) >> 4) % WORDS] <= wdata; wa <= wa + 16; wleft <= wleft - 1;
                if (wlast != (wleft == 1)) errors = errors + 1;
                if (wleft == 1) begin wact <= 0; wready <= 0; bvalid <= 1; bresp <= 0; end
            end
            if (bvalid && bready) bvalid <= 0;
            // ---- lecture
            arready <= 0;
            if (!ract && arvalid && !arready && rnd(3) == 0) begin
                arready <= 1; ra <= araddr; rleft <= arlen + 1; ract <= 1;
                if ((araddr & 32'hFFF) != 0 || arlen != 255) errors = errors + 1;
            end
            if (rvalid && rready) begin rvalid <= 0; if (rlast) begin ract <= 0; rlast <= 0; end end
            if (ract && rleft > 0 && (!rvalid || rready) && rnd(4) != 0) begin
                rvalid <= 1; rdata <= mem[((ra - BASE) >> 4) % WORDS]; rresp <= 0; rlast <= (rleft == 1);
                ra <= ra + 16; rleft <= rleft - 1;
            end
        end
    end
endmodule
