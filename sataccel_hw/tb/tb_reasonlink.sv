// Unit test of rtl/reasonlink_core.v against tb/axi_mem.sv (xsim):
// logits for a few cells in memory -> CELL pushes -> COMMIT -> check the
// answerStack and miscCounters words the core writes. Covers argmax ties
// (lowest digit wins), negative logits, both NVDLA output layouts (channel
// group 32, and group 8 with channels 8-9 one surface stride later), an odd
// num_vars (last beat's upper word must be 0), misc[3] = literal count, and a
// second puzzle after CLEAR.
`timescale 1ns / 1ps
module tb_reasonlink;
    logic clk = 0, rstn = 0;
    always #5 clk = ~clk;

    // AXI-Lite
    logic [15:0] awaddr, araddr;
    logic awvalid = 0, wvalid = 0, bready = 1, arvalid = 0, rready = 1;
    logic [31:0] wdata, rdata;
    logic awready, wready, bvalid, arready, rvalid;
    logic [1:0] bresp, rresp;
    // AXI4 master
    logic [31:0] m_awaddr, m_araddr;
    logic [7:0] m_awlen, m_arlen, m_wstrb;
    logic [2:0] m_awsize, m_arsize;
    logic [1:0] m_awburst, m_arburst, m_bresp, m_rresp;
    logic m_awvalid, m_awready, m_wlast, m_wvalid, m_wready, m_bvalid, m_bready;
    logic m_arvalid, m_arready, m_rlast, m_rvalid, m_rready;
    logic [63:0] m_wdata, m_rdata;
    logic [0:0] bid, rid;

    reasonlink_core dut (
        .ap_clk(clk), .ap_rst_n(rstn),
        .s_axi_awaddr(awaddr), .s_axi_awvalid(awvalid), .s_axi_awready(awready),
        .s_axi_wdata(wdata), .s_axi_wstrb(4'hf), .s_axi_wvalid(wvalid), .s_axi_wready(wready),
        .s_axi_bresp(bresp), .s_axi_bvalid(bvalid), .s_axi_bready(bready),
        .s_axi_araddr(araddr), .s_axi_arvalid(arvalid), .s_axi_arready(arready),
        .s_axi_rdata(rdata), .s_axi_rresp(rresp), .s_axi_rvalid(rvalid), .s_axi_rready(rready),
        .m_axi_awaddr(m_awaddr), .m_axi_awlen(m_awlen), .m_axi_awsize(m_awsize), .m_axi_awburst(m_awburst),
        .m_axi_awvalid(m_awvalid), .m_axi_awready(m_awready),
        .m_axi_wdata(m_wdata), .m_axi_wstrb(m_wstrb), .m_axi_wlast(m_wlast), .m_axi_wvalid(m_wvalid),
        .m_axi_wready(m_wready), .m_axi_bresp(m_bresp), .m_axi_bvalid(m_bvalid), .m_axi_bready(m_bready),
        .m_axi_araddr(m_araddr), .m_axi_arlen(m_arlen), .m_axi_arsize(m_arsize), .m_axi_arburst(m_arburst),
        .m_axi_arvalid(m_arvalid), .m_axi_arready(m_arready),
        .m_axi_rdata(m_rdata), .m_axi_rresp(m_rresp), .m_axi_rlast(m_rlast), .m_axi_rvalid(m_rvalid),
        .m_axi_rready(m_rready));

    axi_mem #(.ID_W(1), .READ_LATENCY(7)) mem (
        .clk(clk), .rstn(rstn),
        .awid(1'b0), .awaddr(m_awaddr), .awlen(m_awlen), .awsize(m_awsize), .awburst(m_awburst),
        .awvalid(m_awvalid), .awready(m_awready),
        .wdata(m_wdata), .wstrb(m_wstrb), .wlast(m_wlast), .wvalid(m_wvalid), .wready(m_wready),
        .bid(bid), .bresp(m_bresp), .bvalid(m_bvalid), .bready(m_bready),
        .arid(1'b0), .araddr(m_araddr), .arlen(m_arlen), .arsize(m_arsize), .arburst(m_arburst),
        .arvalid(m_arvalid), .arready(m_arready),
        .rid(rid), .rdata(m_rdata), .rresp(m_rresp), .rlast(m_rlast), .rvalid(m_rvalid), .rready(m_rready));

    task automatic wr(input [15:0] a, input [31:0] d);
        @(posedge clk);
        awaddr <= a; wdata <= d; awvalid <= 1; wvalid <= 1;
        do @(posedge clk); while (!(awvalid && awready) && !(wvalid && wready));
        // both are accepted in the same cycle by this slave
        awvalid <= 0; wvalid <= 0;
        do @(posedge clk); while (!bvalid);
    endtask

    task automatic rd(input [15:0] a, output [31:0] d);
        @(posedge clk);
        araddr <= a; arvalid <= 1;
        do @(posedge clk); while (!arready);
        arvalid <= 0;
        do @(posedge clk); while (!rvalid);
        d = rdata;
    endtask

    // little-endian 32-bit word at a byte address of axi_mem
    function automatic logic [31:0] mword(input longint unsigned a);
        logic [63:0] w = mem.rd_word(a);
        return a[2] ? w[63:32] : w[31:0];
    endfunction

    // logits (10 int8, channel c in byte c) at a, group-32 layout
    task automatic put_logits32(input longint unsigned a, input logic signed [7:0] l [10]);
        logic [63:0] w0, w1;
        for (int c = 0; c < 8; c++) w0[8*c +: 8] = l[c];
        w1 = 0;
        w1[7:0] = l[8]; w1[15:8] = l[9];
        mem.mem[a >> 3] = w0;
        mem.mem[(a >> 3) + 1] = w1;
    endtask

    task automatic put_logits8(input longint unsigned a, input longint unsigned surf, input logic signed [7:0] l [10]);
        logic [63:0] w0, w1;
        for (int c = 0; c < 8; c++) w0[8*c +: 8] = l[c];
        w1 = 0;
        w1[7:0] = l[8]; w1[15:8] = l[9];
        mem.mem[a >> 3] = w0;
        mem.mem[(a + surf) >> 3] = w1;
    endtask

    int errors = 0;
    task automatic expect_word(input string what, input logic [31:0] got, input logic [31:0] want);
        if (got !== want) begin
            $display("FAIL %s: got %0d (0x%h), want %0d", what, got, got, want);
            errors++;
        end
    endtask

    localparam longint ANS = 64'h1000_0000, MISC = 64'h1001_0000, LOG = 64'h2000_0000;

    task automatic run_puzzle(input int group, input int nv, input int cells[$], input logic signed [7:0] logits[$][10],
                              input int want_digit[$]);
        logic [31:0] st;
        int n = cells.size();
        wr(16'h0004, 1);                          // clear
        wr(16'h0010, nv);
        wr(16'h0014, group == 8 ? (32'h1000 | 8) : 32);   // surface stride 4096 for group 8
        for (int k = 0; k < n; k++) begin
            longint a = LOG + 64'h40 * k;
            if (group == 8) put_logits8(a, 4096, logits[k]); else put_logits32(a, logits[k]);
            wr(16'h0018, a);
            wr(16'h001C, cells[k]);
        end
        wr(16'h0020, 1);                          // commit
        do rd(16'h0024, st); while (!st[1]);
        if (st[2]) begin $display("FAIL error flag set"); errors++; end
        expect_word("literal count (status)", st[15:8], n);
        for (int k = 0; k < n; k++)
            expect_word($sformatf("answerStack[%0d]", k), mword(ANS + 4 * k), 9 * cells[k] + want_digit[k]);
        for (int k = n; k < nv + (nv % 2); k++)           // zeros, incl. an odd num_vars' padding word
            expect_word($sformatf("answerStack[%0d] (zero)", k), mword(ANS + 4 * k), 0);
        for (int k = 0; k < 8; k++)
            expect_word($sformatf("misc[%0d]", k), mword(MISC + 4 * k), k == 3 ? n : 100 + k);
    endtask

    initial begin
        logic signed [7:0] L [$][10];
        logic [31:0] id;
        repeat (5) @(posedge clk);
        rstn <= 1;
        repeat (5) @(posedge clk);
        rd(16'h0000, id);
        expect_word("ID", id, 32'h524C0001);
        wr(16'h0008, ANS);
        wr(16'h000C, MISC);
        for (int k = 0; k < 8; k++) wr(16'h0040 + 4 * k, 100 + k);
        // pre-fill answerStack with garbage: must be overwritten with zeros
        for (int k = 0; k < 16; k++) mem.mem[(ANS >> 3) + k] = 64'hFFFF_FFFF_FFFF_FFFF;

        // Puzzle 1, group 32: cells 0, 5, 80; logits incl. a tie (digits 3 and 7 -> 3),
        // channel 0 largest (ignored: argmax over 1..9), all negative
        L.push_back('{8'sd100, 8'sd1, 8'sd2, 8'sd50, 8'sd3, 8'sd4, 8'sd5, 8'sd50, 8'sd6, 8'sd7});
        L.push_back('{8'sd0, -8'sd5, -8'sd3, -8'sd9, -8'sd1, -8'sd7, -8'sd2, -8'sd8, -8'sd4, -8'sd6});
        L.push_back('{8'sd0, 8'sd0, 8'sd0, 8'sd0, 8'sd0, 8'sd0, 8'sd0, 8'sd0, 8'sd0, 8'sd127});
        run_puzzle(32, 9, '{0, 5, 80}, L, '{3, 4, 9});
        // Puzzle 2, group 8: channels 8, 9 come from the second surface
        L.delete();
        L.push_back('{8'sd0, 8'sd1, 8'sd1, 8'sd1, 8'sd1, 8'sd1, 8'sd1, 8'sd1, 8'sd90, 8'sd2});
        L.push_back('{8'sd0, 8'sd10, 8'sd20, 8'sd30, 8'sd40, 8'sd50, 8'sd60, 8'sd70, 8'sd80, 8'sd90});
        run_puzzle(8, 12, '{3, 4}, L, '{8, 9});
        $display("%s: %0d errors", errors ? "TB_REASONLINK FAIL" : "TB_REASONLINK PASS", errors);
        $finish;
    end
endmodule
