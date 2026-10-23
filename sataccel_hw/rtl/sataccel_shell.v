// SAT-Accel shell: APB decode, the APB -> AXI4-Lite bridge to the kernels'
// control ports, and the cycle counters the evaluation needs (busy, memory
// active, memory stall). Register map: sataccel_hw/drv/sataccel_drv.h.
//
//   paddr[19:16] == 4'hF  shell registers (answered here, no wait states)
//   otherwise             kernel control windows, through apb2axil
//
// Counters watch the subsystem's AXI4 master (inputs only: the shell taps
// the handshakes, it does not sit in the data path). "busy" starts on the
// APB write that sets the solver's ap_start and ends on the rising edge of
// the solver's ap_done interrupt (the driver enables it).
module sataccel_shell #(
    parameter [19:0] SOLVER_CTRL = 20'h60000
) (
    input  wire        clk,
    input  wire        rstn,
    // APB slave (from ESP)
    input  wire        psel,
    input  wire        penable,
    input  wire        pwrite,
    input  wire [31:0] paddr,
    input  wire [31:0] pwdata,
    output wire [31:0] prdata,
    output wire        pready,
    output wire        pslverr,
    // AXI4-Lite master (to the block design's S_AXI_CTRL)
    output wire [19:0] c_awaddr,
    output wire        c_awvalid,
    input  wire        c_awready,
    output wire [31:0] c_wdata,
    output wire [3:0]  c_wstrb,
    output wire        c_wvalid,
    input  wire        c_wready,
    input  wire [1:0]  c_bresp,
    input  wire        c_bvalid,
    output wire        c_bready,
    output wire [19:0] c_araddr,
    output wire        c_arvalid,
    input  wire        c_arready,
    input  wire [31:0] c_rdata,
    input  wire [1:0]  c_rresp,
    input  wire        c_rvalid,
    output wire        c_rready,
    // Taps on the AXI4 memory master
    input  wire        m_arvalid,
    input  wire        m_arready,
    input  wire        m_rvalid,
    input  wire        m_rready,
    input  wire        m_awvalid,
    input  wire        m_awready,
    input  wire        m_wvalid,
    input  wire        m_wready,
    // Solver completion (ap_done interrupt)
    input  wire        solver_irq
);
    localparam [31:0] ID_VALUE = 32'h5A7A0001;

    wire shell_sel = psel && paddr[19:16] == 4'hF;
    wire kern_sel = psel && paddr[19:16] != 4'hF;

    // Kernel windows
    wire [31:0] k_prdata;
    wire k_pready, k_pslverr;
    apb2axil #(.ADDR_WIDTH(20)) bridge (
        .clk(clk), .rstn(rstn),
        .psel(kern_sel), .penable(penable), .pwrite(pwrite), .paddr(paddr[19:0]), .pwdata(pwdata),
        .prdata(k_prdata), .pready(k_pready), .pslverr(k_pslverr),
        .m_awaddr(c_awaddr), .m_awvalid(c_awvalid), .m_awready(c_awready),
        .m_wdata(c_wdata), .m_wstrb(c_wstrb), .m_wvalid(c_wvalid), .m_wready(c_wready),
        .m_bresp(c_bresp), .m_bvalid(c_bvalid), .m_bready(c_bready),
        .m_araddr(c_araddr), .m_arvalid(c_arvalid), .m_arready(c_arready),
        .m_rdata(c_rdata), .m_rresp(c_rresp), .m_rvalid(c_rvalid), .m_rready(c_rready));

    // Counters
    reg [63:0] cyc, busy_cyc, mem_active, mem_stall, rd_beats, wr_beats;
    reg busy, irq_q;
    reg [31:0] hi_latch;
    wire clear = shell_sel && penable && pwrite && paddr[15:0] == 16'h0004 && pwdata[0];
    wire start_solver = kern_sel && !penable && pwrite && paddr[19:0] == SOLVER_CTRL && pwdata[0];
    wire irq_rise = solver_irq && !irq_q;
    wire rd_beat = m_rvalid && m_rready;
    wire wr_beat = m_wvalid && m_wready;
    wire any_beat = rd_beat || wr_beat || (m_arvalid && m_arready) || (m_awvalid && m_awready);
    wire any_stall = (m_arvalid && !m_arready) || (m_awvalid && !m_awready) || (m_wvalid && !m_wready);

    always @(posedge clk) begin
        if (!rstn || clear) begin
            cyc <= 64'd0;
            busy_cyc <= 64'd0;
            mem_active <= 64'd0;
            mem_stall <= 64'd0;
            rd_beats <= 64'd0;
            wr_beats <= 64'd0;
            busy <= 1'b0;
            irq_q <= 1'b0;
        end else begin
            irq_q <= solver_irq;
            cyc <= cyc + 64'd1;
            if (start_solver) busy <= 1'b1;
            else if (irq_rise) busy <= 1'b0;
            if (busy) busy_cyc <= busy_cyc + 64'd1;
            if (any_beat) mem_active <= mem_active + 64'd1;
            if (any_stall) mem_stall <= mem_stall + 64'd1;
            if (rd_beat) rd_beats <= rd_beats + 64'd1;
            if (wr_beat) wr_beats <= wr_beats + 64'd1;
        end
    end

    // Shell register reads: reading a counter's low word latches its high word
    reg [31:0] s_rdata;
    reg [63:0] sel64;
    always @(*) begin
        case (paddr[15:3])
            13'h02: sel64 = cyc;          // 0x10
            13'h03: sel64 = busy_cyc;     // 0x18
            13'h04: sel64 = mem_active;   // 0x20
            13'h05: sel64 = mem_stall;    // 0x28
            13'h06: sel64 = rd_beats;     // 0x30
            13'h07: sel64 = wr_beats;     // 0x38
            default: sel64 = 64'd0;
        endcase
        case (paddr[15:0])
            16'h0000: s_rdata = ID_VALUE;
            16'h0008: s_rdata = {30'd0, solver_irq, busy};
            default:  s_rdata = paddr[2] ? hi_latch : sel64[31:0];
        endcase
    end
    always @(posedge clk) begin
        if (!rstn) hi_latch <= 32'd0;
        else if (shell_sel && penable && !pwrite && !paddr[2]) hi_latch <= sel64[63:32];
    end

    assign prdata  = shell_sel ? s_rdata : k_prdata;
    assign pready  = shell_sel ? 1'b1 : k_pready;
    assign pslverr = shell_sel ? 1'b0 : k_pslverr;
endmodule
