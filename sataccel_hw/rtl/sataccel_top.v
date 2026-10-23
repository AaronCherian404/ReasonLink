// SAT-Accel subsystem top: the shell (APB decode, APB -> AXI-Lite bridge,
// counters) plus the block design (seven HLS kernels, k2k.cfg streams,
// SmartConnects). Interface, as ESP's third-party socket expects:
//   - 32-bit APB slave (configuration)
//   - one 64-bit AXI4 master, 32-bit addresses (memory)
//   - one level interrupt (solver ap_done)
// SmartConnect's master port carries no AXI IDs; m_*id are driven 0 (the
// socket allows up to 10 ID bits) and response IDs are ignored.
module sataccel_top #(
    parameter ID_W = 4
) (
    input  wire            clk,
    input  wire            rstn,
    // APB
    input  wire            psel,
    input  wire            penable,
    input  wire            pwrite,
    input  wire [31:0]     paddr,
    input  wire [31:0]     pwdata,
    output wire [31:0]     prdata,
    output wire            pready,
    output wire            pslverr,
    // AXI4 master
    output wire [ID_W-1:0] m_awid,
    output wire [31:0]     m_awaddr,
    output wire [7:0]      m_awlen,
    output wire [2:0]      m_awsize,
    output wire [1:0]      m_awburst,
    output wire            m_awlock,
    output wire [3:0]      m_awcache,
    output wire [2:0]      m_awprot,
    output wire [3:0]      m_awqos,
    output wire            m_awvalid,
    input  wire            m_awready,
    output wire [63:0]     m_wdata,
    output wire [7:0]      m_wstrb,
    output wire            m_wlast,
    output wire            m_wvalid,
    input  wire            m_wready,
    input  wire [ID_W-1:0] m_bid,
    input  wire [1:0]      m_bresp,
    input  wire            m_bvalid,
    output wire            m_bready,
    output wire [ID_W-1:0] m_arid,
    output wire [31:0]     m_araddr,
    output wire [7:0]      m_arlen,
    output wire [2:0]      m_arsize,
    output wire [1:0]      m_arburst,
    output wire            m_arlock,
    output wire [3:0]      m_arcache,
    output wire [2:0]      m_arprot,
    output wire [3:0]      m_arqos,
    output wire            m_arvalid,
    input  wire            m_arready,
    input  wire [ID_W-1:0] m_rid,
    input  wire [63:0]     m_rdata,
    input  wire [1:0]      m_rresp,
    input  wire            m_rlast,
    input  wire            m_rvalid,
    output wire            m_rready,
    // Interrupt
    output wire            irq
);
    // Shell <-> block design control (AXI4-Lite)
    wire [19:0] c_awaddr, c_araddr;
    wire [31:0] c_wdata, c_rdata;
    wire [3:0]  c_wstrb;
    wire [1:0]  c_bresp, c_rresp;
    wire c_awvalid, c_awready, c_wvalid, c_wready, c_bvalid, c_bready;
    wire c_arvalid, c_arready, c_rvalid, c_rready;
    wire solver_irq;

    sataccel_shell shell (
        .clk(clk), .rstn(rstn),
        .psel(psel), .penable(penable), .pwrite(pwrite), .paddr(paddr), .pwdata(pwdata),
        .prdata(prdata), .pready(pready), .pslverr(pslverr),
        .c_awaddr(c_awaddr), .c_awvalid(c_awvalid), .c_awready(c_awready),
        .c_wdata(c_wdata), .c_wstrb(c_wstrb), .c_wvalid(c_wvalid), .c_wready(c_wready),
        .c_bresp(c_bresp), .c_bvalid(c_bvalid), .c_bready(c_bready),
        .c_araddr(c_araddr), .c_arvalid(c_arvalid), .c_arready(c_arready),
        .c_rdata(c_rdata), .c_rresp(c_rresp), .c_rvalid(c_rvalid), .c_rready(c_rready),
        .m_arvalid(m_arvalid), .m_arready(m_arready), .m_rvalid(m_rvalid), .m_rready(m_rready),
        .m_awvalid(m_awvalid), .m_awready(m_awready), .m_wvalid(m_wvalid), .m_wready(m_wready),
        .solver_irq(solver_irq));

    sataccel_bd_wrapper bd (
        .ap_clk(clk),
        .ap_rst_n(rstn),
        .S_AXI_CTRL_awaddr(c_awaddr), .S_AXI_CTRL_awprot(3'b000), .S_AXI_CTRL_awvalid(c_awvalid),
        .S_AXI_CTRL_awready(c_awready),
        .S_AXI_CTRL_wdata(c_wdata), .S_AXI_CTRL_wstrb(c_wstrb), .S_AXI_CTRL_wvalid(c_wvalid),
        .S_AXI_CTRL_wready(c_wready),
        .S_AXI_CTRL_bresp(c_bresp), .S_AXI_CTRL_bvalid(c_bvalid), .S_AXI_CTRL_bready(c_bready),
        .S_AXI_CTRL_araddr(c_araddr), .S_AXI_CTRL_arprot(3'b000), .S_AXI_CTRL_arvalid(c_arvalid),
        .S_AXI_CTRL_arready(c_arready),
        .S_AXI_CTRL_rdata(c_rdata), .S_AXI_CTRL_rresp(c_rresp), .S_AXI_CTRL_rvalid(c_rvalid),
        .S_AXI_CTRL_rready(c_rready),
        .M_AXI_awaddr(m_awaddr), .M_AXI_awlen(m_awlen), .M_AXI_awsize(m_awsize), .M_AXI_awburst(m_awburst),
        .M_AXI_awlock(m_awlock), .M_AXI_awcache(m_awcache), .M_AXI_awprot(m_awprot), .M_AXI_awqos(m_awqos),
        .M_AXI_awvalid(m_awvalid), .M_AXI_awready(m_awready),
        .M_AXI_wdata(m_wdata), .M_AXI_wstrb(m_wstrb), .M_AXI_wlast(m_wlast), .M_AXI_wvalid(m_wvalid),
        .M_AXI_wready(m_wready),
        .M_AXI_bresp(m_bresp), .M_AXI_bvalid(m_bvalid), .M_AXI_bready(m_bready),
        .M_AXI_araddr(m_araddr), .M_AXI_arlen(m_arlen), .M_AXI_arsize(m_arsize), .M_AXI_arburst(m_arburst),
        .M_AXI_arlock(m_arlock), .M_AXI_arcache(m_arcache), .M_AXI_arprot(m_arprot), .M_AXI_arqos(m_arqos),
        .M_AXI_arvalid(m_arvalid), .M_AXI_arready(m_arready),
        .M_AXI_rdata(m_rdata), .M_AXI_rresp(m_rresp), .M_AXI_rlast(m_rlast), .M_AXI_rvalid(m_rvalid),
        .M_AXI_rready(m_rready),
        .solver_irq(solver_irq));

    assign m_awid = {ID_W{1'b0}};
    assign m_arid = {ID_W{1'b0}};
    assign irq = solver_irq;
endmodule
