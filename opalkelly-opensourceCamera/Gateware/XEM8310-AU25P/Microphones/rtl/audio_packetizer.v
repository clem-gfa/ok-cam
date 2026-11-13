// Paquets audio 64 canaux -> flux AXI-Stream 32 bits (pour une pipe FrontPanel a blocs, comme la video).
//
// Paquet = 1024 mots de 32 bits (4096 octets), little-endian :
//   mots 0..7   en-tete :
//     0  magic 0x4D494331 ("MIC1")
//     1  seq : numero de paquet (incremente a chaque paquet, envoye ou abandonne -> un trou = paquet perdu)
//     2  ts[31:0]   horodatage (ticks de l'horloge audio) du 1er echantillon du paquet
//     3  ts[63:32]
//     4  {flags[15:0], nsamp[15:0]}   nsamp = NS ; flags bit0 = au moins un paquet abandonne avant celui-ci
//     5  vcnt : nombre de debuts de trame video vus
//     6  vts[31:0]  horodatage du dernier debut de trame video
//     7  vts[63:32]
//   mots 8..1015 : NS (=21) instants d'echantillonnage x 48 mots ; un instant = 64 canaux x 24 bits
//                  (canal c : bits [24c +: 24] de l'instant, entier signe, little-endian)
//   mots 1016..1023 : bourrage a zero
// Si le FIFO aval n'a pas la place d'un paquet entier au debut du paquet (space_ok = 0), le paquet est
// abandonne en entier (jamais de paquet tronque).
module audio_packetizer #(
    parameter CH = 64,
    parameter W  = 24,
    parameter NS = 21
) (
    input  wire              clk,
    input  wire              rst,
    input  wire              run,         // flux micros valide (mic_clkgen.run)
    input  wire [CH*W-1:0]   in,          // un instant d'echantillonnage (sortie du FIR)
    input  wire              in_valid,
    input  wire [63:0]       ts,
    input  wire [63:0]       vts,
    input  wire [31:0]       vcnt,
    input  wire              space_ok,    // le FIFO aval peut accepter 1024 mots
    output wire [31:0]       m_tdata,
    output wire              m_tvalid,
    output wire              m_tlast,
    input  wire              m_tready,
    output reg  [31:0]       overruns,    // in_valid recus pendant l'emission d'un instant precedent (ne devrait pas arriver)
    output reg  [31:0]       dropped      // paquets abandonnes
);
    localparam PW = CH * W / 32;          // mots par instant (48)
    localparam [31:0] MAGIC = 32'h4D494331;
    localparam S_IDLE = 2'd0, S_HDR = 2'd1, S_PAY = 2'd2, S_PAD = 2'd3;

    reg [CH*W-1:0] vec;
    reg [1:0]  st;
    reg [5:0]  wc;
    reg [7:0]  scount;
    reg [31:0] seq;
    reg        send, last, lost;
    reg [63:0] h_ts, h_vts;
    reg [31:0] h_vcnt;
    reg        h_lost;

    wire start_pkt = (scount == 0);
    wire send_now  = start_pkt ? (space_ok && run) : send;

    assign m_tvalid = (st != S_IDLE);
    assign m_tlast  = (st == S_PAD) && (wc == 6'd7);
    reg [31:0] hw;
    always @* begin
        case (wc[2:0])
            3'd0: hw = MAGIC;
            3'd1: hw = seq;
            3'd2: hw = h_ts[31:0];
            3'd3: hw = h_ts[63:32];
            3'd4: hw = {15'd0, h_lost, 16'd0} | NS;
            3'd5: hw = h_vcnt;
            3'd6: hw = h_vts[31:0];
            default: hw = h_vts[63:32];
        endcase
    end
    assign m_tdata = (st == S_HDR) ? hw : (st == S_PAY) ? vec[32*wc +: 32] : 32'd0;

    always @(posedge clk) begin
        if (rst) begin
            st <= S_IDLE; wc <= 0; scount <= 0; seq <= 0; send <= 0; last <= 0; lost <= 0;
            overruns <= 0; dropped <= 0;
        end else begin
            if (m_tvalid && m_tready) begin
                wc <= wc + 6'd1;
                case (st)
                    S_HDR: if (wc == 6'd7) begin st <= S_PAY; wc <= 0; end
                    S_PAY: if (wc == PW - 1) begin
                               wc <= 0;
                               if (last) st <= S_PAD; else st <= S_IDLE;
                           end
                    S_PAD: if (wc == 6'd7) begin st <= S_IDLE; wc <= 0; seq <= seq + 32'd1; end
                    default: ;
                endcase
            end
            if (in_valid) begin
                if (!run) begin
                    scount <= 0;
                end else if (st != S_IDLE) begin
                    overruns <= overruns + 32'd1;
                end else begin
                    vec <= in;
                    scount <= (scount == NS - 1) ? 8'd0 : scount + 8'd1;
                    last   <= (scount == NS - 1);
                    if (start_pkt) begin
                        send <= send_now;
                        h_ts <= ts; h_vts <= vts; h_vcnt <= vcnt; h_lost <= lost;
                        if (send_now) lost <= 1'b0;
                    end
                    if (send_now) begin
                        st <= start_pkt ? S_HDR : S_PAY; wc <= 0;
                    end else if (scount == NS - 1) begin
                        seq <= seq + 32'd1; dropped <= dropped + 32'd1; lost <= 1'b1;
                    end else if (start_pkt) begin
                        // 1er instant d'un paquet abandonne
                    end
                end
            end
        end
    end
endmodule
