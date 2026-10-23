// APB (32-bit) slave -> AXI4-Lite (32-bit) master.
// One transfer at a time: an APB access waits (pready low) until the AXI-Lite
// write response or read data returns. pslverr reports a non-OKAY response.
module apb2axil #(
    parameter ADDR_WIDTH = 20
) (
    input  wire                  clk,
    input  wire                  rstn,
    // APB slave
    input  wire                  psel,
    input  wire                  penable,
    input  wire                  pwrite,
    input  wire [ADDR_WIDTH-1:0] paddr,
    input  wire [31:0]           pwdata,
    output reg  [31:0]           prdata,
    output reg                   pready,
    output reg                   pslverr,
    // AXI4-Lite master
    output reg  [ADDR_WIDTH-1:0] m_awaddr,
    output reg                   m_awvalid,
    input  wire                  m_awready,
    output reg  [31:0]           m_wdata,
    output wire [3:0]            m_wstrb,
    output reg                   m_wvalid,
    input  wire                  m_wready,
    input  wire [1:0]            m_bresp,
    input  wire                  m_bvalid,
    output reg                   m_bready,
    output reg  [ADDR_WIDTH-1:0] m_araddr,
    output reg                   m_arvalid,
    input  wire                  m_arready,
    input  wire [31:0]           m_rdata,
    input  wire [1:0]            m_rresp,
    input  wire                  m_rvalid,
    output reg                   m_rready
);
    localparam IDLE = 2'd0, WRITE = 2'd1, READ = 2'd2, DONE = 2'd3;
    reg [1:0] state;

    assign m_wstrb = 4'hf;

    always @(posedge clk) begin
        if (!rstn) begin
            state <= IDLE;
            pready <= 1'b0;
            pslverr <= 1'b0;
            prdata <= 32'd0;
            m_awvalid <= 1'b0;
            m_wvalid <= 1'b0;
            m_bready <= 1'b0;
            m_arvalid <= 1'b0;
            m_rready <= 1'b0;
            m_awaddr <= {ADDR_WIDTH{1'b0}};
            m_araddr <= {ADDR_WIDTH{1'b0}};
            m_wdata <= 32'd0;
        end else begin
            pready <= 1'b0;
            case (state)
            IDLE:
                if (psel && !penable) begin          // APB setup phase
                    if (pwrite) begin
                        m_awaddr <= paddr;
                        m_wdata <= pwdata;
                        m_awvalid <= 1'b1;
                        m_wvalid <= 1'b1;
                        m_bready <= 1'b1;
                        state <= WRITE;
                    end else begin
                        m_araddr <= paddr;
                        m_arvalid <= 1'b1;
                        m_rready <= 1'b1;
                        state <= READ;
                    end
                end
            WRITE: begin
                if (m_awready) m_awvalid <= 1'b0;
                if (m_wready) m_wvalid <= 1'b0;
                if (m_bvalid && m_bready) begin
                    m_bready <= 1'b0;
                    pslverr <= m_bresp != 2'b00;
                    pready <= 1'b1;
                    state <= DONE;
                end
            end
            READ: begin
                if (m_arready) m_arvalid <= 1'b0;
                if (m_rvalid && m_rready) begin
                    m_rready <= 1'b0;
                    prdata <= m_rdata;
                    pslverr <= m_rresp != 2'b00;
                    pready <= 1'b1;
                    state <= DONE;
                end
            end
            DONE:                                    // access phase completes this cycle
                state <= IDLE;
            endcase
        end
    end
endmodule
