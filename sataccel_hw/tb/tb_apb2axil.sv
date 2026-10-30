// Unit test of rtl/apb2axil.v under both APB timings it sees:
//   std  textbook APB: setup phase (psel, !penable), then access phase
//   esp  ESP's noc2apb: writes as std; reads with psel and penable together
//        from the first cycle, held until pready
// AXI-Lite side: a 16-word register file with a few cycles of latency.
// Run: xvlog ../rtl/apb2axil.v tb_apb2axil.sv && xelab tb_apb2axil -R
// (no timescale: the RTL under test has none)
module tb_apb2axil;
    logic clk = 0, rstn = 0;
    always #5 clk = ~clk;

    logic        psel = 0, penable = 0, pwrite = 0;
    logic [19:0] paddr = 0;
    logic [31:0] pwdata = 0;
    wire  [31:0] prdata;
    wire         pready, pslverr;

    wire [19:0] awaddr, araddr;
    wire        awvalid, wvalid, bready, arvalid, rready;
    wire [31:0] wdata;
    wire [3:0]  wstrb;
    logic       awready = 0, wready = 0, bvalid = 0, arready = 0, rvalid = 0;
    logic [31:0] rdata = 0;

    apb2axil dut (
        .clk(clk), .rstn(rstn),
        .psel(psel), .penable(penable), .pwrite(pwrite), .paddr(paddr), .pwdata(pwdata),
        .prdata(prdata), .pready(pready), .pslverr(pslverr),
        .m_awaddr(awaddr), .m_awvalid(awvalid), .m_awready(awready),
        .m_wdata(wdata), .m_wstrb(wstrb), .m_wvalid(wvalid), .m_wready(wready),
        .m_bresp(2'b00), .m_bvalid(bvalid), .m_bready(bready),
        .m_araddr(araddr), .m_arvalid(arvalid), .m_arready(arready),
        .m_rdata(rdata), .m_rresp(2'b00), .m_rvalid(rvalid), .m_rready(rready));

    // AXI-Lite slave: accepts address and data, responds 3 cycles later
    logic [31:0] regs [16];
    always @(posedge clk) begin
        awready <= 0; wready <= 0; arready <= 0;
        if (bvalid && bready) bvalid <= 0;
        if (rvalid && rready) rvalid <= 0;
        if (awvalid && wvalid && !awready && !bvalid) begin
            awready <= 1; wready <= 1;
            regs[awaddr[5:2]] <= wdata;
            repeat (3) @(posedge clk);
            bvalid <= 1;
        end else if (arvalid && !arready && !rvalid) begin
            arready <= 1;
            rdata <= regs[araddr[5:2]];
            repeat (3) @(posedge clk);
            rvalid <= 1;
        end
    end

    int errors = 0;

    task automatic wait_ready(input string what);
        int n = 0;
        do begin
            @(posedge clk);
            if (++n == 1000) begin
                $display("FAIL: %s: no pready after 1000 cycles", what);
                $finish;
            end
        end while (!pready);
    endtask

    task automatic apb_write(input logic [19:0] a, input logic [31:0] d);
        @(posedge clk);
        psel <= 1; penable <= 0; pwrite <= 1; paddr <= a; pwdata <= d;
        @(posedge clk);
        penable <= 1;
        wait_ready($sformatf("write %h", a));
        psel <= 0; penable <= 0;
    endtask

    task automatic apb_read(input bit esp, input logic [19:0] a, output logic [31:0] d);
        @(posedge clk);
        psel <= 1; penable <= esp; pwrite <= 0; paddr <= a;
        if (!esp) begin
            @(posedge clk);
            penable <= 1;
        end
        wait_ready($sformatf("%s read %h", esp ? "esp" : "std", a));
        d = prdata;
        psel <= 0; penable <= 0;
    endtask

    initial begin
        logic [31:0] d;
        repeat (5) @(posedge clk);
        rstn <= 1;
        for (int i = 0; i < 16; i++) apb_write(i * 4, 32'hA5000000 + i);
        for (int esp = 0; esp < 2; esp++) begin
            for (int i = 0; i < 16; i++) begin
                apb_read(esp, i * 4, d);
                if (d !== 32'hA5000000 + i) begin
                    $display("FAIL: %s read %0d = %h", esp ? "esp" : "std", i, d);
                    errors++;
                end
            end
            // back to back: a read straight after a write, and two reads
            apb_write(20'h8, 32'h12345678);
            apb_read(esp, 20'h8, d);
            if (d !== 32'h12345678) errors++;
            apb_read(esp, 20'h4, d);
            if (d !== 32'hA5000001) errors++;
            apb_write(20'h8, 32'hA5000002);
        end
        $display("%s apb2axil: std and esp reads, %0d errors", errors ? "FAIL" : "PASS", errors);
        $finish;
    end
endmodule
