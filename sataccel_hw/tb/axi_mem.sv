// Behavioral AXI4 slave memory (64-bit data) for the SAT-Accel testbench.
// - Sparse: words never written read as 0 (like the zeroed reserved memory).
// - One read burst and one write burst at a time (arready/awready low while
//   busy): legal AXI, conservative for performance.
// - READ_LATENCY cycles from AR acceptance to the first R beat, then one beat
//   per cycle; a write responds one cycle after its last W beat.
// - Bursts: INCR (ESP's third-party socket supports only INCR; others are
//   counted as errors). Narrow beats (size < 8 bytes) are served correctly
//   and counted, since the ESP socket's handling of them must be checked.
module axi_mem #(
    parameter ID_W = 4,
    parameter READ_LATENCY = 30
) (
    input  logic            clk,
    input  logic            rstn,
    input  logic [ID_W-1:0] awid,
    input  logic [31:0]     awaddr,
    input  logic [7:0]      awlen,
    input  logic [2:0]      awsize,
    input  logic [1:0]      awburst,
    input  logic            awvalid,
    output logic            awready,
    input  logic [63:0]     wdata,
    input  logic [7:0]      wstrb,
    input  logic            wlast,
    input  logic            wvalid,
    output logic            wready,
    output logic [ID_W-1:0] bid,
    output logic [1:0]      bresp,
    output logic            bvalid,
    input  logic            bready,
    input  logic [ID_W-1:0] arid,
    input  logic [31:0]     araddr,
    input  logic [7:0]      arlen,
    input  logic [2:0]      arsize,
    input  logic [1:0]      arburst,
    input  logic            arvalid,
    output logic            arready,
    output logic [ID_W-1:0] rid,
    output logic [63:0]     rdata,
    output logic [1:0]      rresp,
    output logic            rlast,
    output logic            rvalid,
    input  logic            rready
);
    logic [63:0] mem [longint unsigned];   // key: byte address >> 3
    longint unsigned n_narrow_rd = 0, n_narrow_wr = 0, n_bad_burst = 0;
    // Value of words never written (+fill=<hex>; default 0). A garbage fill
    // checks that the accelerator reads only what was loaded or it wrote.
    logic [63:0] fill = 64'd0;
    initial if ($value$plusargs("fill=%h", fill)) $display("axi_mem: unwritten words read as %h", fill);

    function automatic logic [63:0] rd_word(input longint unsigned a);
        return mem.exists(a >> 3) ? mem[a >> 3] : fill;
    endfunction

    task automatic load(input string path);
        int fd, n;
        longint unsigned a;
        logic [63:0] d;
        fd = $fopen(path, "r");
        if (fd == 0) $fatal(1, "axi_mem: cannot open %s", path);
        n = 0;
        while ($fscanf(fd, "%h %h\n", a, d) == 2) begin
            mem[a >> 3] = d;
            n++;
        end
        $fclose(fd);
        $display("axi_mem: loaded %0d words from %s", n, path);
    endtask

    task automatic dump(input int fd, input longint unsigned a, input longint unsigned bytes);
        for (longint unsigned x = a & ~64'h7; x < a + bytes; x += 8)
            $fdisplay(fd, "%h %h", x, rd_word(x));
    endtask

    // Reads
    logic r_busy;
    logic [31:0] r_addr;
    logic [7:0] r_left;
    logic [2:0] r_size;
    logic [ID_W-1:0] r_id;
    int r_wait;
    assign arready = !r_busy && !rvalid;
    assign rresp = 2'b00;
    always_ff @(posedge clk) begin
        if (!rstn) begin
            r_busy <= 0; rvalid <= 0; rlast <= 0; rdata <= 0; rid <= 0;
        end else begin
            if (rvalid && rready) begin
                rvalid <= 0; rlast <= 0;
            end
            if (arvalid && arready) begin
                if (arburst != 2'b01 && arlen != 0) n_bad_burst++;
                if (arsize != 3'd3) n_narrow_rd++;
                r_busy <= 1; r_addr <= araddr; r_left <= arlen; r_size <= arsize; r_id <= arid;
                r_wait <= READ_LATENCY;
            end else if (r_busy) begin
                if (r_wait > 0) r_wait <= r_wait - 1;
                else if (!rvalid || rready) begin
                    rvalid <= 1;
                    rdata <= rd_word(r_addr);
                    rid <= r_id;
                    rlast <= r_left == 0;
                    r_addr <= r_addr + (32'd1 << r_size);
                    if (r_left == 0) r_busy <= 0;
                    else r_left <= r_left - 1;
                end
            end
        end
    end

    // Writes
    logic w_busy;
    logic [31:0] w_addr;
    logic [2:0] w_size;
    logic [ID_W-1:0] w_id;
    assign awready = !w_busy && !bvalid;
    assign wready = w_busy;
    assign bresp = 2'b00;
    always_ff @(posedge clk) begin
        if (!rstn) begin
            w_busy <= 0; bvalid <= 0; bid <= 0;
        end else begin
            if (bvalid && bready) bvalid <= 0;
            if (awvalid && awready) begin
                if (awburst != 2'b01 && awlen != 0) n_bad_burst++;
                if (awsize != 3'd3) n_narrow_wr++;
                w_busy <= 1; w_addr <= awaddr; w_size <= awsize; w_id <= awid;
            end else if (w_busy && wvalid) begin
                logic [63:0] word;
                word = rd_word(w_addr);
                for (int b = 0; b < 8; b++)
                    if (wstrb[b]) word[8*b +: 8] = wdata[8*b +: 8];
                mem[w_addr >> 3] = word;
                w_addr <= w_addr + (32'd1 << w_size);
                if (wlast) begin
                    w_busy <= 0; bvalid <= 1; bid <= w_id;
                end
            end
        end
    end
endmodule
