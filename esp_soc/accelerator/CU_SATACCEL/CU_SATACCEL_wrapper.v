// ESP third-party wrapper for the SAT-Accel subsystem (sataccel_hw).
// Maps sataccel_top's ports onto the names ESP's socketgen derives from
// CU_SATACCEL.xml (axi_prefix "sataccel_", no APB prefix, clock "clk",
// active-low reset "rstn", interrupt "sataccel_irq"). No logic besides tying
// the AXI fields SAT-Accel does not drive (IDs, atop, region).
//
// Register window: ESP gives each third-party tile 1 MB and passes
// paddr & 0x0FFFFFFF; sataccel_shell decodes paddr[19:0] (map in
// sataccel_hw/drv/sataccel_drv.h).
module CU_SATACCEL_wrapper (
    input  wire        clk,
    input  wire        rstn,
    // APB
    input  wire        psel,
    input  wire        penable,
    input  wire [31:0] paddr,
    input  wire        pwrite,
    input  wire [31:0] pwdata,
    output wire [31:0] prdata,
    output wire        pready,
    output wire        pslverr,
    // AXI4 master
    output wire [7:0]  sataccel_awid,
    output wire [31:0] sataccel_awaddr,
    output wire [7:0]  sataccel_awlen,
    output wire [2:0]  sataccel_awsize,
    output wire [1:0]  sataccel_awburst,
    output wire        sataccel_awlock,
    output wire [3:0]  sataccel_awcache,
    output wire [2:0]  sataccel_awprot,
    output wire        sataccel_awvalid,
    output wire [3:0]  sataccel_awqos,
    output wire [5:0]  sataccel_awatop,
    output wire [3:0]  sataccel_awregion,
    input  wire        sataccel_awready,
    output wire [63:0] sataccel_wdata,
    output wire [7:0]  sataccel_wstrb,
    output wire        sataccel_wlast,
    output wire        sataccel_wvalid,
    input  wire        sataccel_wready,
    output wire [7:0]  sataccel_arid,
    output wire [31:0] sataccel_araddr,
    output wire [7:0]  sataccel_arlen,
    output wire [2:0]  sataccel_arsize,
    output wire [1:0]  sataccel_arburst,
    output wire        sataccel_arlock,
    output wire [3:0]  sataccel_arcache,
    output wire [2:0]  sataccel_arprot,
    output wire        sataccel_arvalid,
    output wire [3:0]  sataccel_arqos,
    output wire [3:0]  sataccel_arregion,
    input  wire        sataccel_arready,
    output wire        sataccel_rready,
    input  wire [7:0]  sataccel_rid,
    input  wire [63:0] sataccel_rdata,
    input  wire [1:0]  sataccel_rresp,
    input  wire        sataccel_rlast,
    input  wire        sataccel_rvalid,
    output wire        sataccel_bready,
    input  wire [7:0]  sataccel_bid,
    input  wire [1:0]  sataccel_bresp,
    input  wire        sataccel_bvalid,
    // Completion interrupt (level, solver ap_done; cleared by the driver)
    output wire        sataccel_irq
);
    wire [3:0] awid_unused, arid_unused;

    sataccel_top acc (
        .clk(clk), .rstn(rstn),
        .psel(psel), .penable(penable), .pwrite(pwrite), .paddr(paddr), .pwdata(pwdata),
        .prdata(prdata), .pready(pready), .pslverr(pslverr),
        .m_awid(awid_unused), .m_awaddr(sataccel_awaddr), .m_awlen(sataccel_awlen),
        .m_awsize(sataccel_awsize), .m_awburst(sataccel_awburst), .m_awlock(sataccel_awlock),
        .m_awcache(sataccel_awcache), .m_awprot(sataccel_awprot), .m_awqos(sataccel_awqos),
        .m_awvalid(sataccel_awvalid), .m_awready(sataccel_awready),
        .m_wdata(sataccel_wdata), .m_wstrb(sataccel_wstrb), .m_wlast(sataccel_wlast),
        .m_wvalid(sataccel_wvalid), .m_wready(sataccel_wready),
        .m_bid(4'd0), .m_bresp(sataccel_bresp), .m_bvalid(sataccel_bvalid), .m_bready(sataccel_bready),
        .m_arid(arid_unused), .m_araddr(sataccel_araddr), .m_arlen(sataccel_arlen),
        .m_arsize(sataccel_arsize), .m_arburst(sataccel_arburst), .m_arlock(sataccel_arlock),
        .m_arcache(sataccel_arcache), .m_arprot(sataccel_arprot), .m_arqos(sataccel_arqos),
        .m_arvalid(sataccel_arvalid), .m_arready(sataccel_arready),
        .m_rid(4'd0), .m_rdata(sataccel_rdata), .m_rresp(sataccel_rresp), .m_rlast(sataccel_rlast),
        .m_rvalid(sataccel_rvalid), .m_rready(sataccel_rready),
        .irq(sataccel_irq));

    assign sataccel_awid = 8'd0;
    assign sataccel_arid = 8'd0;
    assign sataccel_awatop = 6'd0;
    assign sataccel_awregion = 4'd0;
    assign sataccel_arregion = 4'd0;
endmodule
