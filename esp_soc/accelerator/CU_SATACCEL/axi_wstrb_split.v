// AXI4 write-strobe adapter between SAT-Accel's AXI master and ESP's
// third-party tile socket (64-bit data, one write burst at a time).
//
// ESP's AXI-to-NoC proxy (rtl/sockets/proxy/axislv2noc.vhd) ignores WSTRB: it
// writes the bytes that AWSIZE and the address select, so a full-width beat
// writes all 8 bytes. SAT-Accel's HLS ports store 32-bit values as full-width
// beats with partial strobes (0x0f, 0xf0) and pad bursts with empty beats
// (0x00), which then overwrite their neighbours with stale or zero data.
//
// Each upstream beat is replayed as single-beat writes of only its enabled
// bytes, as naturally aligned 8-, 4-, 2- or 1-byte transfers (beats with no
// strobe are dropped). The upstream burst gets its B response after every
// downstream write has been acknowledged (SLVERR if any of them failed).
// Reads do not pass through here.
module axi_wstrb_split #(
    parameter MAX_OUTSTANDING = 8
) (
    input  wire        clk,
    input  wire        rstn,
    // from SAT-Accel
    input  wire [31:0] s_awaddr,
    input  wire [7:0]  s_awlen,
    input  wire [2:0]  s_awsize,
    input  wire [1:0]  s_awburst,
    input  wire        s_awvalid,
    output wire        s_awready,
    input  wire [63:0] s_wdata,
    input  wire [7:0]  s_wstrb,
    input  wire        s_wlast,
    input  wire        s_wvalid,
    output wire        s_wready,
    output wire [1:0]  s_bresp,
    output wire        s_bvalid,
    input  wire        s_bready,
    // to ESP
    output wire [31:0] m_awaddr,
    output wire [7:0]  m_awlen,
    output wire [2:0]  m_awsize,
    output wire [1:0]  m_awburst,
    output wire        m_awvalid,
    input  wire        m_awready,
    output wire [63:0] m_wdata,
    output wire [7:0]  m_wstrb,
    output wire        m_wlast,
    output wire        m_wvalid,
    input  wire        m_wready,
    input  wire [1:0]  m_bresp,
    input  wire        m_bvalid,
    output wire        m_bready
);
    // upstream burst
    reg        busy, done, bvalid_r, berr;
    reg [31:0] addr;              // address of the current beat
    reg [7:0]  left;              // beats after the current one
    reg [2:0]  size;
    reg [1:0]  burst;
    // current beat
    reg        loaded, last;
    reg [63:0] data;
    reg [7:0]  mask;              // bytes not yet written
    // downstream write in flight
    reg        aw_p, w_p;
    reg [31:0] sub_addr;
    reg [2:0]  sub_size;
    reg [7:0]  sub_strb;
    reg [4:0]  outstanding;       // downstream writes without B

    // Next downstream write: the lowest enabled byte, widened to the largest
    // naturally aligned transfer whose bytes are all enabled
    reg [2:0] lane, nsize;
    reg [7:0] nstrb;
    integer b;
    always @(*) begin
        lane = 3'd0;
        for (b = 7; b >= 0; b = b - 1)
            if (mask[b]) lane = b;
        if (mask == 8'hff) begin
            nsize = 3'd3; nstrb = 8'hff;
        end else if (lane[1:0] == 2'd0 && ((mask >> lane) & 8'h0f) == 8'h0f) begin
            nsize = 3'd2; nstrb = 8'h0f << lane;
        end else if (lane[0] == 1'b0 && ((mask >> lane) & 8'h03) == 8'h03) begin
            nsize = 3'd1; nstrb = 8'h03 << lane;
        end else begin
            nsize = 3'd0; nstrb = 8'h01 << lane;
        end
    end

    wire launch = loaded && !aw_p && !w_p && mask != 8'h00 && outstanding < MAX_OUTSTANDING;
    wire beat_done = loaded && !aw_p && !w_p && mask == 8'h00;
    wire [31:0] step = 32'd1 << size;

    assign s_awready = !busy && !bvalid_r;
    assign s_wready = busy && !done && !loaded;
    assign s_bvalid = bvalid_r;
    assign s_bresp = berr ? 2'b10 : 2'b00;

    assign m_awaddr = sub_addr;
    assign m_awlen = 8'd0;
    assign m_awsize = sub_size;
    assign m_awburst = 2'b01;
    assign m_awvalid = aw_p;
    assign m_wdata = data;
    assign m_wstrb = sub_strb;
    assign m_wlast = 1'b1;
    assign m_wvalid = w_p;
    assign m_bready = 1'b1;

    always @(posedge clk) begin
        if (!rstn) begin
            busy <= 1'b0; done <= 1'b0; bvalid_r <= 1'b0; berr <= 1'b0;
            loaded <= 1'b0; aw_p <= 1'b0; w_p <= 1'b0; outstanding <= 5'd0;
        end else begin
            if (s_awvalid && s_awready) begin
                busy <= 1'b1; done <= 1'b0; berr <= 1'b0;
                addr <= s_awaddr; left <= s_awlen; size <= s_awsize; burst <= s_awburst;
            end
            if (s_wvalid && s_wready) begin
                loaded <= 1'b1; data <= s_wdata; mask <= s_wstrb; last <= s_wlast;
            end
            if (launch) begin
                aw_p <= 1'b1; w_p <= 1'b1;
                sub_addr <= {addr[31:3], lane}; sub_size <= nsize; sub_strb <= nstrb;
                mask <= mask & ~nstrb;
            end
            if (m_awvalid && m_awready) aw_p <= 1'b0;
            if (m_wvalid && m_wready) w_p <= 1'b0;
            outstanding <= outstanding + (launch ? 5'd1 : 5'd0) - (m_bvalid ? 5'd1 : 5'd0);
            if (m_bvalid && m_bresp != 2'b00) berr <= 1'b1;
            if (beat_done) begin
                loaded <= 1'b0;
                if (last || left == 8'd0)
                    done <= 1'b1;
                else begin
                    left <= left - 8'd1;
                    if (burst != 2'b00) addr <= (addr & ~(step - 32'd1)) + step;
                end
            end
            if (busy && done && outstanding == 5'd0 && !aw_p && !w_p) begin
                busy <= 1'b0; bvalid_r <= 1'b1;
            end
            if (s_bvalid && s_bready) bvalid_r <= 1'b0;
        end
    end
endmodule
