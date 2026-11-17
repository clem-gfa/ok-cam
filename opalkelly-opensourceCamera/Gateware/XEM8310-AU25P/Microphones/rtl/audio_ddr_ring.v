// Tampon en anneau en DDR4 pour les paquets audio (domaine d'horloge de l'interface AXI de la DDR4).
//
//   FIFO d'entree (stage, 128 bits, lecture FWFT)  --AXI ecriture-->  anneau en DDR4  --AXI lecture-->  FIFO de sortie (128 bits)
//
// Un paquet = PKT_BEATS (256) mots de 128 bits = 4096 octets = une rafale AXI INCR de 256 beats (alignee sur
// 4 Kio : ne franchit jamais une frontiere de 4 Kio). L'anneau contient RING_PKTS paquets (puissance de 2) a
// partir de RING_BASE. Une seule rafale d'ecriture et une seule de lecture a la fois.
//
// Ecriture : des que le FIFO d'entree contient un paquet complet et que l'anneau n'est pas plein.
// Lecture  : des que l'anneau contient un paquet complet ET que le FIFO de sortie a la place d'un paquet.
// L'anneau ne compte un paquet qu'apres la reponse d'ecriture (B) : on ne lit jamais un paquet non termine.
//
// rst_req (domaine clk, deja synchronise) : n'autorise plus de nouvelle rafale ; quand tout est au repos
// (idle = 1) les pointeurs sont remis a zero. Les FIFO ne doivent etre vides (reset) que lorsque idle = 1.
module audio_ddr_ring #(
    parameter [31:0] RING_BASE  = 32'hA0000000,
    parameter        RING_AW    = 14,            // 2^14 paquets x 4 Kio = 64 Mio
    parameter        STG_CNT_W  = 9,             // largeur de stg_avail (beats lisibles du FIFO d'entree)
    parameter        OUT_CNT_W  = 11,            // largeur de out_wr_count (beats du FIFO de sortie)
    parameter        OUT_DEPTH  = 2048           // profondeur du FIFO de sortie en beats
) (
    input  wire                 clk,
    input  wire                 aresetn,
    input  wire                 rst_req,

    // FIFO d'entree (lecture, FWFT)
    input  wire [127:0]         stg_dout,
    input  wire                 stg_empty,
    input  wire [STG_CNT_W-1:0] stg_avail,       // beats disponibles
    output wire                 stg_rd_en,

    // FIFO de sortie (ecriture)
    output wire [127:0]         out_din,
    output wire                 out_wr_en,
    input  wire                 out_full,
    input  wire [OUT_CNT_W-1:0] out_wr_count,    // beats presents (cote ecriture)

    // AXI4 maitre, 128 bits
    output reg  [31:0]          m_axi_awaddr,
    output wire [7:0]           m_axi_awlen,
    output wire [2:0]           m_axi_awsize,
    output wire [1:0]           m_axi_awburst,
    output wire                 m_axi_awlock,
    output wire [3:0]           m_axi_awcache,
    output wire [2:0]           m_axi_awprot,
    output wire [3:0]           m_axi_awqos,
    output reg                  m_axi_awvalid,
    input  wire                 m_axi_awready,
    output wire [127:0]         m_axi_wdata,
    output wire [15:0]          m_axi_wstrb,
    output wire                 m_axi_wlast,
    output wire                 m_axi_wvalid,
    input  wire                 m_axi_wready,
    input  wire [1:0]           m_axi_bresp,
    input  wire                 m_axi_bvalid,
    output reg                  m_axi_bready,
    output reg  [31:0]          m_axi_araddr,
    output wire [7:0]           m_axi_arlen,
    output wire [2:0]           m_axi_arsize,
    output wire [1:0]           m_axi_arburst,
    output wire                 m_axi_arlock,
    output wire [3:0]           m_axi_arcache,
    output wire [2:0]           m_axi_arprot,
    output wire [3:0]           m_axi_arqos,
    output reg                  m_axi_arvalid,
    input  wire                 m_axi_arready,
    input  wire [127:0]         m_axi_rdata,
    input  wire [1:0]           m_axi_rresp,
    input  wire                 m_axi_rlast,
    input  wire                 m_axi_rvalid,
    output wire                 m_axi_rready,

    // etat
    output wire                 idle,
    output wire                 space_ok,        // l'anneau peut recevoir au moins 2 paquets de plus
    output wire [15:0]          used_pkts,       // paquets dans l'anneau
    output reg  [15:0]          peak_pkts,       // maximum depuis le dernier reset
    output reg  [15:0]          axi_errors       // reponses AXI differentes de OKAY
);
    localparam PKT_BEATS = 256;
    localparam [RING_AW:0] RING_PKTS = (1 << RING_AW);

    assign m_axi_awlen   = 8'd255;
    assign m_axi_awsize  = 3'd4;       // 16 octets
    assign m_axi_awburst = 2'b01;      // INCR
    assign m_axi_awlock  = 1'b0;
    assign m_axi_awcache = 4'b0011;
    assign m_axi_awprot  = 3'b000;
    assign m_axi_awqos   = 4'b0000;
    assign m_axi_arlen   = 8'd255;
    assign m_axi_arsize  = 3'd4;
    assign m_axi_arburst = 2'b01;
    assign m_axi_arlock  = 1'b0;
    assign m_axi_arcache = 4'b0011;
    assign m_axi_arprot  = 3'b000;
    assign m_axi_arqos   = 4'b0000;
    assign m_axi_wstrb   = 16'hFFFF;

    reg [RING_AW:0]   wr_done, rd_done;     // paquets termines (compteurs, un bit de plus que l'indice)
    wire [RING_AW:0]  used = wr_done - rd_done;
    assign used_pkts = {{(15 - RING_AW){1'b0}}, used};
    assign space_ok  = (used + 2) < RING_PKTS;

    // ---- ecriture --------------------------------------------------------------------------------
    localparam W_IDLE = 2'd0, W_AW = 2'd1, W_DATA = 2'd2, W_RESP = 2'd3;
    reg [1:0] wst;
    reg [8:0] wbeat;
    assign m_axi_wdata  = stg_dout;
    assign m_axi_wvalid = (wst == W_DATA) && !stg_empty;
    assign m_axi_wlast  = (wbeat == PKT_BEATS - 1);
    assign stg_rd_en    = m_axi_wvalid && m_axi_wready;

    // ---- lecture ---------------------------------------------------------------------------------
    localparam R_IDLE = 2'd0, R_AR = 2'd1, R_DATA = 2'd2;
    reg [1:0] rst_;
    reg [8:0] rbeat;
    assign out_din      = m_axi_rdata;
    assign m_axi_rready = (rst_ == R_DATA) && !out_full;
    assign out_wr_en    = m_axi_rvalid && m_axi_rready;

    wire [OUT_CNT_W:0] out_free = OUT_DEPTH - out_wr_count;
    wire               w_can = !rst_req && (stg_avail >= PKT_BEATS) && (used < RING_PKTS);
    wire               r_can = !rst_req && (used != 0) && (out_free >= PKT_BEATS + 8);

    assign idle = (wst == W_IDLE) && (rst_ == R_IDLE);

    always @(posedge clk) begin
        if (!aresetn) begin
            wst <= W_IDLE; rst_ <= R_IDLE; wr_done <= 0; rd_done <= 0; wbeat <= 0; rbeat <= 0;
            m_axi_awvalid <= 1'b0; m_axi_arvalid <= 1'b0; m_axi_bready <= 1'b0;
            m_axi_awaddr <= RING_BASE; m_axi_araddr <= RING_BASE; peak_pkts <= 0; axi_errors <= 0;
        end else begin
            // ---- ecriture
            case (wst)
                W_IDLE: if (w_can) begin
                    m_axi_awaddr  <= RING_BASE + (wr_done[RING_AW-1:0] << 12);
                    m_axi_awvalid <= 1'b1;
                    wst <= W_AW;
                end
                W_AW: if (m_axi_awready) begin m_axi_awvalid <= 1'b0; wbeat <= 0; wst <= W_DATA; end
                W_DATA: if (m_axi_wvalid && m_axi_wready) begin
                    wbeat <= wbeat + 9'd1;
                    if (wbeat == PKT_BEATS - 1) begin m_axi_bready <= 1'b1; wst <= W_RESP; end
                end
                W_RESP: if (m_axi_bvalid) begin
                    m_axi_bready <= 1'b0;
                    if (m_axi_bresp != 2'b00) axi_errors <= axi_errors + 16'd1;
                    wr_done <= wr_done + 1'b1;
                    if (used + 1 > peak_pkts) peak_pkts <= used + 1;
                    wst <= W_IDLE;
                end
            endcase
            // ---- lecture
            case (rst_)
                R_IDLE: if (r_can) begin
                    m_axi_araddr  <= RING_BASE + (rd_done[RING_AW-1:0] << 12);
                    m_axi_arvalid <= 1'b1;
                    rst_ <= R_AR;
                end
                R_AR: if (m_axi_arready) begin m_axi_arvalid <= 1'b0; rbeat <= 0; rst_ <= R_DATA; end
                R_DATA: if (m_axi_rvalid && m_axi_rready) begin
                    rbeat <= rbeat + 9'd1;
                    if (m_axi_rresp != 2'b00) axi_errors <= axi_errors + 16'd1;
                    if (m_axi_rlast) begin rd_done <= rd_done + 1'b1; rst_ <= R_IDLE; end
                end
            endcase
            // ---- remise a zero des pointeurs quand tout est au repos
            if (rst_req && idle) begin
                wr_done <= 0; rd_done <= 0; peak_pkts <= 0;
            end
        end
    end
endmodule
