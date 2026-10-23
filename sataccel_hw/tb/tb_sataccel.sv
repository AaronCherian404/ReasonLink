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
//   #...                     comment
//
// Plusargs: +ops=<file> +mem=<file> +dump=<file> [+max_poll_cycles=N]
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

    sataccel_top #(.ID_W(ID_W)) dut (
        .clk(clk), .rstn(rstn),
        .psel(psel), .penable(penable), .pwrite(pwrite), .paddr(paddr), .pwdata(pwdata),
        .prdata(prdata), .pready(pready), .pslverr(pslverr),
        .m_awid(awid), .m_awaddr(awaddr), .m_awlen(awlen), .m_awsize(awsize), .m_awburst(awburst),
        .m_awlock(), .m_awcache(), .m_awprot(), .m_awqos(), .m_awvalid(awvalid), .m_awready(awready),
        .m_wdata(wdata), .m_wstrb(wstrb), .m_wlast(wlast), .m_wvalid(wvalid), .m_wready(wready),
        .m_bid(bid), .m_bresp(bresp), .m_bvalid(bvalid), .m_bready(bready),
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

    // APB master tasks (setup phase, then access phase until pready)
    task automatic apb_write(input logic [31:0] a, input logic [31:0] d);
        @(posedge clk);
        psel <= 1; penable <= 0; pwrite <= 1; paddr <= a; pwdata <= d;
        @(posedge clk);
        penable <= 1;
        do @(posedge clk); while (!pready);
        if (pslverr) $error("APB write error at %h", a);
        psel <= 0; penable <= 0;
    endtask

    task automatic apb_read(input logic [31:0] a, output logic [31:0] d);
        @(posedge clk);
        psel <= 1; penable <= 0; pwrite <= 0; paddr <= a;
        @(posedge clk);
        penable <= 1;
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
        $display("axi_mem: narrow reads=%0d narrow writes=%0d non-INCR bursts=%0d",
                 mem.n_narrow_rd, mem.n_narrow_wr, mem.n_bad_burst);
        $display("DONE sim_cycle=%0d", now_cycles());
        $finish;
    end
endmodule
