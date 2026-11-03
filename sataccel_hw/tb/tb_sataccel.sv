// Standalone RTL testbench for the SAT-Accel subsystem (sataccel_top).
//
// The stimulus is the real driver (sataccel_hw/drv) run with its record
// backend (tools/tb_gen): an ops file of register accesses plus a memory
// image. This testbench replays the ops over APB against axi_mem and dumps
// the memory ranges the driver reads back; tools/tb_check decodes them.
//
// Ops file (hex numbers):
//   W <off> <val>            APB write
//   P <off> <mask> <val>     poll (APB reads) until (rd & mask) == val
//   D <addr> <bytes>         dump memory range to +dump file
//   C <label>                print the shell counters
//   R <off>                  print a register (APB read)
//   #...                     comment
//
// Plusargs: +ops=<file> +mem=<file> +dump=<file> [+max_poll_cycles=N] [+fill=<hex64>]
//           [+esp_wr] axi_mem ignores write strobes, as ESP's AXI socket does
//           [+split]  SAT-Accel's writes go through the tile's axi_wstrb_split
//                     (esp_soc/accelerator/CU_SATACCEL), as in the ESP SoC
`timescale 1ns / 1ps
module tb_sataccel;
    parameter int READ_LATENCY = 30;
    parameter int ID_W = 4;

    logic clk = 0, rstn = 0;
    always #5 clk = ~clk;   // 100 MHz

    // APB
    logic psel = 0, penable = 0, pwrite = 0;
    logic [31:0] paddr = 0, pwdata = 0;
    logic [31:0] prdata;
    logic pready, pslverr;

    // AXI4 master
    logic [ID_W-1:0] awid, bid, arid, rid;
    logic [31:0] awaddr, araddr;
    logic [7:0] awlen, arlen;
    logic [2:0] awsize, arsize;
    logic [1:0] awburst, arburst, bresp, rresp;
    logic awvalid, awready, wlast, wvalid, wready, bvalid, bready;
    logic arvalid, arready, rlast, rvalid, rready;
    logic [63:0] wdata, rdata;
    logic [7:0] wstrb;
    logic irq;

    // SAT-Accel's write channels, before the optional write adapter
    logic [ID_W-1:0] d_awid, d_bid;
    logic [31:0] d_awaddr;
    logic [7:0] d_awlen, d_wstrb;
    logic [2:0] d_awsize;
    logic [1:0] d_awburst, d_bresp;
    logic [63:0] d_wdata;
    logic d_awvalid, d_awready, d_wlast, d_wvalid, d_wready, d_bvalid, d_bready;

    sataccel_top #(.ID_W(ID_W)) dut (
        .clk(clk), .rstn(rstn),
        .psel(psel), .penable(penable), .pwrite(pwrite), .paddr(paddr), .pwdata(pwdata),
        .prdata(prdata), .pready(pready), .pslverr(pslverr),
        .m_awid(d_awid), .m_awaddr(d_awaddr), .m_awlen(d_awlen), .m_awsize(d_awsize), .m_awburst(d_awburst),
        .m_awlock(), .m_awcache(), .m_awprot(), .m_awqos(), .m_awvalid(d_awvalid), .m_awready(d_awready),
        .m_wdata(d_wdata), .m_wstrb(d_wstrb), .m_wlast(d_wlast), .m_wvalid(d_wvalid), .m_wready(d_wready),
        .m_bid(d_bid), .m_bresp(d_bresp), .m_bvalid(d_bvalid), .m_bready(d_bready),
        .m_arid(arid), .m_araddr(araddr), .m_arlen(arlen), .m_arsize(arsize), .m_arburst(arburst),
        .m_arlock(), .m_arcache(), .m_arprot(), .m_arqos(), .m_arvalid(arvalid), .m_arready(arready),
        .m_rid(rid), .m_rdata(rdata), .m_rresp(rresp), .m_rlast(rlast), .m_rvalid(rvalid), .m_rready(rready),
        .irq(irq));

    axi_mem #(.ID_W(ID_W), .READ_LATENCY(READ_LATENCY)) mem (
        .clk(clk), .rstn(rstn),
        .awid(awid), .awaddr(awaddr), .awlen(awlen), .awsize(awsize), .awburst(awburst),
        .awvalid(awvalid), .awready(awready),
        .wdata(wdata), .wstrb(wstrb), .wlast(wlast), .wvalid(wvalid), .wready(wready),
        .bid(bid), .bresp(bresp), .bvalid(bvalid), .bready(bready),
        .arid(arid), .araddr(araddr), .arlen(arlen), .arsize(arsize), .arburst(arburst),
        .arvalid(arvalid), .arready(arready),
        .rid(rid), .rdata(rdata), .rresp(rresp), .rlast(rlast), .rvalid(rvalid), .rready(rready));

    bit use_split;
    initial use_split = $test$plusargs("split");
    logic [31:0] sp_awaddr;
    logic [7:0] sp_awlen, sp_wstrb;
    logic [2:0] sp_awsize;
    logic [1:0] sp_awburst, sp_bresp;
    logic [63:0] sp_wdata;
    logic sp_awvalid, sp_awready, sp_wlast, sp_wvalid, sp_wready, sp_bvalid, sp_bready;
    axi_wstrb_split split (
        .clk(clk), .rstn(rstn),
        .s_awaddr(d_awaddr), .s_awlen(d_awlen), .s_awsize(d_awsize), .s_awburst(d_awburst),
        .s_awvalid(d_awvalid && use_split), .s_awready(sp_awready),
        .s_wdata(d_wdata), .s_wstrb(d_wstrb), .s_wlast(d_wlast), .s_wvalid(d_wvalid && use_split),
        .s_wready(sp_wready), .s_bresp(sp_bresp), .s_bvalid(sp_bvalid), .s_bready(d_bready),
        .m_awaddr(sp_awaddr), .m_awlen(sp_awlen), .m_awsize(sp_awsize), .m_awburst(sp_awburst),
        .m_awvalid(sp_awvalid), .m_awready(awready),
        .m_wdata(sp_wdata), .m_wstrb(sp_wstrb), .m_wlast(sp_wlast), .m_wvalid(sp_wvalid), .m_wready(wready),
        .m_bresp(bresp), .m_bvalid(bvalid), .m_bready(sp_bready));
    assign awid    = use_split ? '0 : d_awid;
    assign awaddr  = use_split ? sp_awaddr : d_awaddr;
    assign awlen   = use_split ? sp_awlen : d_awlen;
    assign awsize  = use_split ? sp_awsize : d_awsize;
    assign awburst = use_split ? sp_awburst : d_awburst;
    assign awvalid = use_split ? sp_awvalid : d_awvalid;
    assign wdata   = use_split ? sp_wdata : d_wdata;
    assign wstrb   = use_split ? sp_wstrb : d_wstrb;
    assign wlast   = use_split ? sp_wlast : d_wlast;
    assign wvalid  = use_split ? sp_wvalid : d_wvalid;
    assign bready  = use_split ? sp_bready : d_bready;
    assign d_awready = use_split ? sp_awready : awready;
    assign d_wready  = use_split ? sp_wready : wready;
    assign d_bid     = use_split ? '0 : bid;
    assign d_bresp   = use_split ? sp_bresp : bresp;
    assign d_bvalid  = use_split ? sp_bvalid : bvalid;

    // APB master tasks: writes as setup phase, then access phase until pready
    task automatic apb_write(input logic [31:0] a, input logic [31:0] d);
        @(posedge clk);
        psel <= 1; penable <= 0; pwrite <= 1; paddr <= a; pwdata <= d;
        @(posedge clk);
        penable <= 1;
        do @(posedge clk); while (!pready);
        if (pslverr) $error("APB write error at %h", a);
        psel <= 0; penable <= 0;
    endtask

    // Reads as ESP's noc2apb drives them (default): psel and penable together
    // from the first cycle, no setup phase. +apb=std: setup phase first.
    bit apb_std;
    initial apb_std = $test$plusargs("apb=std");

    task automatic apb_read(input logic [31:0] a, output logic [31:0] d);
        @(posedge clk);
        psel <= 1; penable <= apb_std ? 0 : 1; pwrite <= 0; paddr <= a;
        if (apb_std) begin
            @(posedge clk);
            penable <= 1;
        end
        do @(posedge clk); while (!pready);
        d = prdata;
        if (pslverr) $error("APB read error at %h", a);
        psel <= 0; penable <= 0;
    endtask

    function automatic longint unsigned now_cycles();
        return $time / 10;
    endfunction

    task automatic read64(input logic [31:0] off, output longint unsigned v);
        logic [31:0] lo, hi;
        apb_read(32'hF0000 + off, lo);
        apb_read(32'hF0000 + off + 4, hi);
        v = {hi, lo};
    endtask

    task automatic print_counters(input string label);
        longint unsigned cyc, busy, act, stall, rdb, wrb;
        read64(32'h10, cyc);
        read64(32'h18, busy);
        read64(32'h20, act);
        read64(32'h28, stall);
        read64(32'h30, rdb);
        read64(32'h38, wrb);
        $display("COUNTERS %s cycles=%0d busy=%0d mem_active=%0d mem_stall=%0d rd_beats=%0d wr_beats=%0d sim_cycle=%0d",
                 label, cyc, busy, act, stall, rdb, wrb, now_cycles());
    endtask

    initial begin
        string ops_path, mem_path, dump_path, op, label;
        int fd, dfd, rc;
        longint unsigned max_poll;
        logic [31:0] a, b, c, r;
        if (!$value$plusargs("ops=%s", ops_path)) ops_path = "ops.txt";
        if (!$value$plusargs("mem=%s", mem_path)) mem_path = "mem.txt";
        if (!$value$plusargs("dump=%s", dump_path)) dump_path = "dump.txt";
        if (!$value$plusargs("max_poll_cycles=%d", max_poll)) max_poll = 64'd200_000_000;

        mem.load(mem_path);
        repeat (20) @(posedge clk);
        rstn <= 1;
        repeat (20) @(posedge clk);

        fd = $fopen(ops_path, "r");
        if (fd == 0) $fatal(1, "cannot open %s", ops_path);
        dfd = $fopen(dump_path, "w");
        while (!$feof(fd)) begin
            rc = $fscanf(fd, "%s", op);
            if (rc != 1) break;
            case (op)
            "W": begin
                rc = $fscanf(fd, "%h %h", a, b);
                apb_write(a, b);
            end
            "P": begin
                longint unsigned t0;
                rc = $fscanf(fd, "%h %h %h", a, b, c);
                t0 = now_cycles();
                do begin
                    apb_read(a, r);
                    if (now_cycles() - t0 > max_poll) begin
                        $display("TIMEOUT polling %h (last %h, want %h/%h) after %0d cycles", a, r, c, b, max_poll);
                        print_counters("timeout");
                        $fclose(dfd);
                        $finish;
                    end
                end while ((r & b) != c);
            end
            "D": begin
                rc = $fscanf(fd, "%h %h", a, b);
                mem.dump(dfd, a, b);
            end
            "F": begin   // switch the dump file (one per puzzle in a sequence)
                rc = $fscanf(fd, "%s", label);
                $fclose(dfd);
                dfd = $fopen(label, "w");
                if (dfd == 0) $fatal(1, "cannot open %s", label);
            end
            "R": begin
                rc = $fscanf(fd, "%h", a);
                apb_read(a, r);
                $display("REG %05h %0d", a, r);
            end
            "C": begin
                rc = $fscanf(fd, "%s", label);
                print_counters(label);
            end
            default: begin   // comment: skip rest of line
                rc = $fgets(label, fd);
            end
            endcase
        end
        $fclose(fd);
        $fclose(dfd);
        $display("axi_mem: narrow reads=%0d narrow writes=%0d non-INCR bursts=%0d strobe-dependent beats=%0d%s%s",
                 mem.n_narrow_rd, mem.n_narrow_wr, mem.n_bad_burst, mem.n_strb_dep,
                 mem.esp_wr ? " (strobes ignored)" : "", use_split ? " (write adapter)" : "");
        $display("DONE sim_cycle=%0d", now_cycles());
        $finish;
    end
endmodule
