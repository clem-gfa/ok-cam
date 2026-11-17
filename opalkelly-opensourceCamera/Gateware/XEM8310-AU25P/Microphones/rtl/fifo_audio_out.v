// FIFO de sortie de l'anneau DDR4 : 128 bits (wr_clk, horloge DDR4) -> 32 bits (rd_clk, okClk), lecture non FWFT
// (donnee valide un cycle apres rd_en, comme le FIFO de la video). Macro XPM. Profondeur d'ecriture 8192 beats = 128 Kio = 32768 mots (32 paquets : c'est la quantite maximale qu'une lecture du PC peut ramener d'un coup).
// rst est synchrone a wr_clk (convention xpm_fifo_async). La simulation utilise sim/fifo_ddr_models.v.
module fifo_audio_out (
    input  wire         rst, wr_clk, rd_clk,
    input  wire [127:0] din,
    input  wire         wr_en,
    input  wire         rd_en,
    output wire [31:0]  dout,
    output wire         full,
    output wire         empty,
    output wire [13:0]  wr_data_count,   // beats de 128 bits presents
    output wire [15:0]  rd_data_count    // mots de 32 bits disponibles
);
    wire wr_rst_busy, rd_rst_busy;
    xpm_fifo_async #(
        .CDC_SYNC_STAGES(2), .DOUT_RESET_VALUE("0"), .ECC_MODE("no_ecc"), .FIFO_MEMORY_TYPE("block"),
        .FIFO_READ_LATENCY(1), .FIFO_WRITE_DEPTH(8192), .FULL_RESET_VALUE(0),
        .PROG_EMPTY_THRESH(10), .PROG_FULL_THRESH(10),
        .RD_DATA_COUNT_WIDTH(16), .READ_DATA_WIDTH(32), .READ_MODE("std"), .RELATED_CLOCKS(0),
        .SIM_ASSERT_CHK(0), .USE_ADV_FEATURES("0404"), .WAKEUP_TIME(0),
        .WRITE_DATA_WIDTH(128), .WR_DATA_COUNT_WIDTH(14)
    ) u_xpm (
        .almost_empty(), .almost_full(), .data_valid(), .dbiterr(), .dout(dout), .empty(empty), .full(full),
        .overflow(), .prog_empty(), .prog_full(), .rd_data_count(rd_data_count), .rd_rst_busy(rd_rst_busy),
        .sbiterr(), .underflow(), .wr_ack(), .wr_data_count(wr_data_count), .wr_rst_busy(wr_rst_busy),
        .din(din), .injectdbiterr(1'b0), .injectsbiterr(1'b0), .rd_clk(rd_clk), .rd_en(rd_en & ~rd_rst_busy),
        .rst(rst), .sleep(1'b0), .wr_clk(wr_clk), .wr_en(wr_en & ~wr_rst_busy)
    );
endmodule
