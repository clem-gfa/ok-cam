// FIFO d'entree de l'anneau DDR4 : 32 bits (wr_clk, horloge micros) -> 128 bits (rd_clk, horloge DDR4), FWFT.
// Macro XPM (pas d'IP a configurer : la largeur asymetrique est un parametre). Profondeur d'ecriture 2048 mots (512 beats).
// rst est synchrone a wr_clk (convention xpm_fifo_async). La simulation utilise sim/fifo_ddr_models.v (meme interface).
module fifo_audio_stage (
    input  wire         rst, wr_clk, rd_clk,
    input  wire [31:0]  din,
    input  wire         wr_en,
    input  wire         rd_en,
    output wire [127:0] dout,
    output wire         full,
    output wire         empty,
    output wire [11:0]  wr_data_count,   // mots de 32 bits presents
    output wire [9:0]   rd_data_count    // beats de 128 bits disponibles
);
    wire wr_rst_busy, rd_rst_busy;
    xpm_fifo_async #(
        .CDC_SYNC_STAGES(2), .DOUT_RESET_VALUE("0"), .ECC_MODE("no_ecc"), .FIFO_MEMORY_TYPE("block"),
        .FIFO_READ_LATENCY(0), .FIFO_WRITE_DEPTH(2048), .FULL_RESET_VALUE(0),
        .PROG_EMPTY_THRESH(10), .PROG_FULL_THRESH(10),
        .RD_DATA_COUNT_WIDTH(10), .READ_DATA_WIDTH(128), .READ_MODE("fwft"), .RELATED_CLOCKS(0),
        .SIM_ASSERT_CHK(0), .USE_ADV_FEATURES("0404"), .WAKEUP_TIME(0),
        .WRITE_DATA_WIDTH(32), .WR_DATA_COUNT_WIDTH(12)
    ) u_xpm (
        .almost_empty(), .almost_full(), .data_valid(), .dbiterr(), .dout(dout), .empty(empty), .full(full),
        .overflow(), .prog_empty(), .prog_full(), .rd_data_count(rd_data_count), .rd_rst_busy(rd_rst_busy),
        .sbiterr(), .underflow(), .wr_ack(), .wr_data_count(wr_data_count), .wr_rst_busy(wr_rst_busy),
        .din(din), .injectdbiterr(1'b0), .injectsbiterr(1'b0), .rd_clk(rd_clk), .rd_en(rd_en & ~rd_rst_busy),
        .rst(rst), .sleep(1'b0), .wr_clk(wr_clk), .wr_en(wr_en & ~wr_rst_busy)
    );
endmodule
