// ReasonLink, phase B (separate tiles: lives in the SAT-Accel tile, reads
// NVDLA's output from DRAM). Instantiated in the SAT-Accel block design when
// built with REASONLINK=1: its AXI4 master is one more SmartConnect source,
// its AXI4-Lite slave the control window at 0x70000.
//
//   (i)   literal producer: for each given cell, fetch NVDLA's 10 int8 ip2
//         logits, argmax over digits 1..9 (ties: lowest digit, as P1's
//         rl_argmax_units), literal = 9*cell + digit
//   (ii)  on-chip FIFO: literals go straight to the delta writer (the CPU
//         never sees logits or literals)
//   (iii) delta writer: on COMMIT, the base image in SAT-Accel's buffers gets
//         only the givens as level-0 fixed decisions: answerStack = the
//         literals then zeros (num_vars words), miscCounters[0..7] = the base
//         values with [3] = count (what sa_dev_load_decisions writes)
//   (iv)  overlap: each cell is converted while NVDLA classifies the next
//
// Literals must arrive in increasing cell order (one per cell), which makes
// the prefix sorted and unique, as SAT-Accel's std::set decision store is.
// CLEAR only when not busy (it does not abort AXI transactions).
//
// Registers (byte offsets in the 64 KB window; 32-bit):
//   0x00 ID           RO  0x524C0001 ("RL", version 1)
//   0x04 CTRL         W   bit 0: clear (literals, pending cells, done, error, counters)
//   0x08 ANSWER_ADDR  RW  device address of SAT-Accel's answerStack buffer (4 KB aligned)
//   0x0C MISC_ADDR    RW  device address of its miscCounters buffer (4 KB aligned)
//   0x10 NUM_VARS     RW  answerStack words to write
//   0x14 LAYOUT       RW  NVDLA output tensor layout: [7:0] channel group
//                         (32: channels 0..9 contiguous; 8: channels 8, 9 one
//                         surface stride later), [31:8] surface stride / 256
//   0x18 LOGIT_ADDR   RW  address of the next cell's channel 0 (8-byte aligned)
//   0x1C CELL         W   push {cell = wdata[6:0], LOGIT_ADDR} for conversion
//   0x20 COMMIT       W   write the delta once every pushed cell is converted
//   0x24 STATUS       RO  [0] busy, [1] done, [2] error, [15:8] literals,
//                         [22:16] cells pending
//   0x28 CYC_CONV     RO  cycles spent fetching + converting
//   0x2C CYC_COMMIT   RO  cycles from COMMIT start to done
//   0x40-0x5C MISC_BASE[0..7]  RW  base miscCounters[0..7]
// error: a cell not after the previous one, a misaligned logit address, more
// than 127 cells pending, or more than 128 literals.
module reasonlink_core #(
    parameter ADDR_W = 32
) (
    // (attributes: Vivado's block design associates both AXI interfaces with this clock)
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 ap_clk CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF s_axi:m_axi, ASSOCIATED_RESET ap_rst_n" *)
    input  wire              ap_clk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 ap_rst_n RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire              ap_rst_n,
    // AXI4-Lite slave (control)
    input  wire [15:0]       s_axi_awaddr,
    input  wire              s_axi_awvalid,
    output reg               s_axi_awready,
    input  wire [31:0]       s_axi_wdata,
    input  wire [3:0]        s_axi_wstrb,
    input  wire              s_axi_wvalid,
    output reg               s_axi_wready,
    output wire [1:0]        s_axi_bresp,
    output reg               s_axi_bvalid,
    input  wire              s_axi_bready,
    input  wire [15:0]       s_axi_araddr,
    input  wire              s_axi_arvalid,
    output reg               s_axi_arready,
    output reg  [31:0]       s_axi_rdata,
    output wire [1:0]        s_axi_rresp,
    output reg               s_axi_rvalid,
    input  wire              s_axi_rready,
    // AXI4 master (memory), 64-bit
    output reg  [ADDR_W-1:0] m_axi_awaddr,
    output reg  [7:0]        m_axi_awlen,
    output wire [2:0]        m_axi_awsize,
    output wire [1:0]        m_axi_awburst,
    output reg               m_axi_awvalid,
    input  wire              m_axi_awready,
    output reg  [63:0]       m_axi_wdata,
    output wire [7:0]        m_axi_wstrb,
    output reg               m_axi_wlast,
    output reg               m_axi_wvalid,
    input  wire              m_axi_wready,
    input  wire [1:0]        m_axi_bresp,
    input  wire              m_axi_bvalid,
    output wire              m_axi_bready,
    output reg  [ADDR_W-1:0] m_axi_araddr,
    output reg  [7:0]        m_axi_arlen,
    output wire [2:0]        m_axi_arsize,
    output wire [1:0]        m_axi_arburst,
    output reg               m_axi_arvalid,
    input  wire              m_axi_arready,
    input  wire [63:0]       m_axi_rdata,
    input  wire [1:0]        m_axi_rresp,
    input  wire              m_axi_rlast,
    input  wire              m_axi_rvalid,
    output wire              m_axi_rready
);
    localparam [31:0] ID_VALUE = 32'h524C0001;

    assign s_axi_bresp = 2'b00;
    assign s_axi_rresp = 2'b00;
    assign m_axi_awsize = 3'd3;
    assign m_axi_arsize = 3'd3;
    assign m_axi_awburst = 2'b01;
    assign m_axi_arburst = 2'b01;
    assign m_axi_wstrb = 8'hFF;
    assign m_axi_bready = 1'b1;
    assign m_axi_rready = 1'b1;

    // ---------------- state ----------------
    reg [31:0] answer_addr, misc_addr, num_vars, layout, logit_addr;
    reg [31:0] misc_base [0:7];
    reg [31:0] cyc_conv, cyc_commit;
    reg        done, error, commit_req;
    reg [7:0]  nlit;                      // literals produced so far

    reg [6:0]  rq_cell [0:127];           // request FIFO: {cell, logit address}
    reg [31:0] rq_addr [0:127];
    reg [6:0]  rq_wp, rq_rp;
    reg [31:0] lit [0:127];               // literal FIFO, read in order by the writer

    // conversion
    localparam C_IDLE = 3'd0, C_AR = 3'd1, C_R = 3'd2, C_AR2 = 3'd3, C_R2 = 3'd4, C_ARG = 3'd5;
    reg [2:0]  cs;
    reg [127:0] lg;                       // byte c = channel c
    reg        beat;
    reg [3:0]  d, best_d;
    reg signed [7:0] best;
    reg [6:0]  cur_cell, last_cell;
    reg [31:0] cur_addr;
    reg        any_cell;

    // delta writer
    localparam W_IDLE = 3'd0, W_AW = 3'd1, W_W = 3'd2, W_B = 3'd3, W_DONE = 3'd4;
    reg [2:0]  ws;
    reg [31:0] widx;                      // next answerStack word
    reg        misc_phase;
    reg [4:0]  wleft;
    reg [1:0]  mbeat;
    reg        commit_done;

    // AXI-Lite write side
    reg [15:0] wa;
    reg [31:0] wd;
    reg        have_aw, have_w;
    wire       wr_fire = have_aw && have_w && !s_axi_bvalid;
    wire       clear = wr_fire && wa == 16'h0004 && wd[0];
    wire       push = wr_fire && wa == 16'h001C;

    wire       rq_empty = rq_wp == rq_rp;
    wire [6:0] rq_count = rq_wp - rq_rp;
    wire       busy = cs != C_IDLE || !rq_empty || (ws != W_IDLE && ws != W_DONE);
    wire       conv_idle = cs == C_IDLE && rq_empty;
    wire       group8 = layout[7:0] == 8'd8;
    wire [31:0] surf = {layout[31:8], 8'd0};
    wire signed [7:0] lg_d = lg[8 * d +: 8];
    wire       take = d == 4'd1 || lg_d > best;          // strict >: lowest digit wins ties
    wire [31:0] words_left = num_vars - widx;
    wire [31:0] beats_left = (words_left + 32'd1) >> 1;
    integer    i;

    function [31:0] aword(input [31:0] k);
        aword = k < {24'd0, nlit} ? lit[k[6:0]] : 32'd0;
    endfunction

    // ---------------- AXI-Lite: registers ----------------
    always @(posedge ap_clk) begin
        if (!ap_rst_n) begin
            s_axi_awready <= 1'b1; s_axi_wready <= 1'b1; s_axi_bvalid <= 1'b0;
            have_aw <= 1'b0; have_w <= 1'b0;
            answer_addr <= 0; misc_addr <= 0; num_vars <= 0; layout <= 32'd32; logit_addr <= 0;
            for (i = 0; i < 8; i = i + 1) misc_base[i] <= 0;
            rq_wp <= 0; commit_req <= 1'b0;
        end else begin
            if (s_axi_awvalid && s_axi_awready) begin wa <= s_axi_awaddr; have_aw <= 1'b1; s_axi_awready <= 1'b0; end
            if (s_axi_wvalid && s_axi_wready) begin wd <= s_axi_wdata; have_w <= 1'b1; s_axi_wready <= 1'b0; end
            if (s_axi_bvalid && s_axi_bready) begin
                s_axi_bvalid <= 1'b0; s_axi_awready <= 1'b1; s_axi_wready <= 1'b1;
            end
            if (commit_done) commit_req <= 1'b0;
            if (wr_fire) begin
                have_aw <= 1'b0; have_w <= 1'b0; s_axi_bvalid <= 1'b1;
                case (wa)
                16'h0004: if (wd[0]) begin rq_wp <= rq_rp; commit_req <= 1'b0; end
                16'h0008: answer_addr <= wd;
                16'h000C: misc_addr <= wd;
                16'h0010: num_vars <= wd;
                16'h0014: layout <= wd;
                16'h0018: logit_addr <= wd;
                16'h001C: if (rq_count != 7'd127) begin
                              rq_cell[rq_wp] <= wd[6:0];
                              rq_addr[rq_wp] <= logit_addr;
                              rq_wp <= rq_wp + 1'b1;
                          end
                16'h0020: commit_req <= 1'b1;
                default:  if (wa[15:5] == 11'h002) misc_base[wa[4:2]] <= wd;   // 0x40-0x5C
                endcase
            end
        end
    end

    always @(posedge ap_clk) begin
        if (!ap_rst_n) begin
            s_axi_arready <= 1'b1; s_axi_rvalid <= 1'b0; s_axi_rdata <= 0;
        end else begin
            if (s_axi_rvalid && s_axi_rready) begin s_axi_rvalid <= 1'b0; s_axi_arready <= 1'b1; end
            if (s_axi_arvalid && s_axi_arready) begin
                s_axi_arready <= 1'b0; s_axi_rvalid <= 1'b1;
                case (s_axi_araddr)
                16'h0000: s_axi_rdata <= ID_VALUE;
                16'h0008: s_axi_rdata <= answer_addr;
                16'h000C: s_axi_rdata <= misc_addr;
                16'h0010: s_axi_rdata <= num_vars;
                16'h0014: s_axi_rdata <= layout;
                16'h0018: s_axi_rdata <= logit_addr;
                16'h0024: s_axi_rdata <= {9'd0, rq_count, nlit, 5'd0, error, done, busy};
                16'h0028: s_axi_rdata <= cyc_conv;
                16'h002C: s_axi_rdata <= cyc_commit;
                default:  s_axi_rdata <= s_axi_araddr[15:5] == 11'h002 ? misc_base[s_axi_araddr[4:2]] : 32'd0;
                endcase
            end
        end
    end

    // ---------------- conversion: fetch logits, argmax ----------------
    always @(posedge ap_clk) begin
        if (!ap_rst_n) begin
            cs <= C_IDLE; m_axi_arvalid <= 1'b0; rq_rp <= 0;
            nlit <= 0; error <= 1'b0; any_cell <= 1'b0; cyc_conv <= 0;
        end else if (clear) begin
            nlit <= 0; error <= 1'b0; any_cell <= 1'b0; cyc_conv <= 0;
        end else begin
            if (push && rq_count == 7'd127) error <= 1'b1;
            if (cs != C_IDLE) cyc_conv <= cyc_conv + 1;
            case (cs)
            C_IDLE:
                if (!rq_empty) begin
                    cur_cell <= rq_cell[rq_rp];
                    cur_addr <= rq_addr[rq_rp];
                    if (rq_addr[rq_rp][2:0] != 3'd0) error <= 1'b1;
                    m_axi_araddr <= {rq_addr[rq_rp][31:3], 3'd0};
                    m_axi_arlen <= group8 ? 8'd0 : 8'd1;
                    m_axi_arvalid <= 1'b1;
                    lg <= 0; beat <= 1'b0;
                    cs <= C_AR;
                end
            C_AR: if (m_axi_arready) begin m_axi_arvalid <= 1'b0; cs <= C_R; end
            C_R:
                if (m_axi_rvalid) begin
                    lg[64 * beat +: 64] <= m_axi_rdata;
                    beat <= 1'b1;
                    if (m_axi_rlast) begin
                        if (group8) begin
                            m_axi_araddr <= {cur_addr[31:3], 3'd0} + surf;
                            m_axi_arlen <= 8'd0;
                            m_axi_arvalid <= 1'b1;
                            cs <= C_AR2;
                        end else begin
                            d <= 4'd1; cs <= C_ARG;
                        end
                    end
                end
            C_AR2: if (m_axi_arready) begin m_axi_arvalid <= 1'b0; cs <= C_R2; end
            C_R2:
                if (m_axi_rvalid) begin
                    lg[64 +: 16] <= m_axi_rdata[15:0];   // channels 8, 9
                    if (m_axi_rlast) begin d <= 4'd1; cs <= C_ARG; end
                end
            C_ARG: begin
                if (take) begin best <= lg_d; best_d <= d; end
                if (d == 4'd9) begin
                    if (nlit == 8'd128) error <= 1'b1;
                    else begin
                        lit[nlit[6:0]] <= {25'd0, cur_cell} * 32'd9 + {28'd0, take ? d : best_d};
                        nlit <= nlit + 1'b1;
                    end
                    if (any_cell && cur_cell <= last_cell) error <= 1'b1;
                    last_cell <= cur_cell;
                    any_cell <= 1'b1;
                    rq_rp <= rq_rp + 1'b1;
                    cs <= C_IDLE;
                end else begin
                    d <= d + 1'b1;
                end
            end
            default: cs <= C_IDLE;
            endcase
        end
    end

    // ---------------- delta writer ----------------
    // answerStack: num_vars words, 2 per beat, bursts of up to 16 beats (128 B,
    // never across 4 KB from an aligned base); then miscCounters[0..7] in 4 beats.
    always @(posedge ap_clk) begin
        commit_done <= 1'b0;
        if (!ap_rst_n) begin
            ws <= W_IDLE; m_axi_awvalid <= 1'b0; m_axi_wvalid <= 1'b0; m_axi_wlast <= 1'b0;
            done <= 1'b0; cyc_commit <= 0;
        end else if (clear) begin
            done <= 1'b0; cyc_commit <= 0;
            if (ws == W_DONE) ws <= W_IDLE;
        end else begin
            if (ws != W_IDLE && ws != W_DONE) cyc_commit <= cyc_commit + 1;
            case (ws)
            W_IDLE, W_DONE:
                if (commit_req && conv_idle && !commit_done && !(ws == W_DONE && done)) begin
                    widx <= 0; misc_phase <= 1'b0; done <= 1'b0; cyc_commit <= 0;
                    ws <= W_AW;
                end
            W_AW: begin
                if (!misc_phase) begin
                    m_axi_awaddr <= answer_addr + (widx << 2);
                    m_axi_awlen <= (beats_left > 32'd16 ? 8'd16 : beats_left[7:0]) - 8'd1;
                    wleft <= beats_left > 32'd16 ? 5'd16 : beats_left[4:0];
                end else begin
                    m_axi_awaddr <= misc_addr;
                    m_axi_awlen <= 8'd3;
                    wleft <= 5'd4;
                    mbeat <= 2'd0;
                end
                m_axi_awvalid <= 1'b1;
                ws <= W_W;
            end
            W_W: begin
                if (m_axi_awvalid && m_axi_awready) m_axi_awvalid <= 1'b0;
                if (!m_axi_wvalid || m_axi_wready) begin
                    if (wleft == 5'd0) begin
                        m_axi_wvalid <= 1'b0;
                        m_axi_wlast <= 1'b0;
                        ws <= W_B;
                    end else begin
                        m_axi_wvalid <= 1'b1;
                        m_axi_wlast <= wleft == 5'd1;
                        if (!misc_phase) begin
                            m_axi_wdata <= {widx + 32'd1 < num_vars ? aword(widx + 32'd1) : 32'd0, aword(widx)};
                            widx <= widx + 32'd2;
                        end else begin
                            // beat k = misc words 2k, 2k+1; word 3 is the literal count
                            m_axi_wdata <= {mbeat == 2'd1 ? {24'd0, nlit} : misc_base[{mbeat, 1'b1}],
                                            misc_base[{mbeat, 1'b0}]};
                            mbeat <= mbeat + 1'b1;
                        end
                        wleft <= wleft - 1'b1;
                    end
                end
            end
            W_B: begin
                if (m_axi_awvalid && m_axi_awready) m_axi_awvalid <= 1'b0;
                if (m_axi_bvalid) begin
                    if (!misc_phase && widx < num_vars) ws <= W_AW;
                    else if (!misc_phase) begin misc_phase <= 1'b1; ws <= W_AW; end
                    else begin ws <= W_DONE; done <= 1'b1; commit_done <= 1'b1; end
                end
            end
            default: ws <= W_IDLE;
            endcase
        end
    end
endmodule
