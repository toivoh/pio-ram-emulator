`default_nettype none
`timescale 1ns/1ps

module tb2 #( parameter IO_BITS=2 ) (
	);

	initial begin
		$dumpfile ("tb2.vcd");
		$dumpvars (0, tb2);
		#1;
	end

	reg clk;
	reg rst_n = 0;

	reg run_mode = 0;
	reg [IO_BITS-1:0] rx_pins;
	wire [IO_BITS-1:0] tx_pins;

	sbio2_tester2 #(.IO_BITS(IO_BITS)) tester (
		.clk(clk), .reset(!rst_n),
		.run_mode(run_mode),
		.rx_pins(rx_pins), .tx_pins(tx_pins)
	);

	int running_counter = 0;
	always @(posedge clk) running_counter <= running_counter + 1;

	localparam MAX_TX_BITS = 24;
	localparam TX_COUNT_BITS = 4;
	localparam TX_DELAY_BITS = 8;
	localparam RX_DATA_BITS = 16;
	localparam RX_DELAY_BITS = 16;

	wire [MAX_TX_BITS-1:0] tx_payload0 = tester.tx_payloads[0];
	wire [MAX_TX_BITS-1:0] tx_payload1 = tester.tx_payloads[1];
	wire [MAX_TX_BITS-1:0] tx_payload2 = tester.tx_payloads[2];
	wire [MAX_TX_BITS-1:0] tx_payload3 = tester.tx_payloads[3];

	wire [TX_COUNT_BITS-1:0] tx_count0 = tester.tx_counts[0];
	wire [TX_COUNT_BITS-1:0] tx_count1 = tester.tx_counts[1];
	wire [TX_COUNT_BITS-1:0] tx_count2 = tester.tx_counts[2];
	wire [TX_COUNT_BITS-1:0] tx_count3 = tester.tx_counts[3];

	wire [TX_DELAY_BITS-1:0] tx_delay0 = tester.tx_delays[0];
	wire [TX_DELAY_BITS-1:0] tx_delay1 = tester.tx_delays[1];
	wire [TX_DELAY_BITS-1:0] tx_delay2 = tester.tx_delays[2];
	wire [TX_DELAY_BITS-1:0] tx_delay3 = tester.tx_delays[3];

	wire [RX_DATA_BITS-1:0] rx_payloads0 = tester.rx_payloads[0];
	wire [RX_DATA_BITS-1:0] rx_payloads1 = tester.rx_payloads[1];
	wire [RX_DATA_BITS-1:0] rx_payloads2 = tester.rx_payloads[2];
	wire [RX_DATA_BITS-1:0] rx_payloads3 = tester.rx_payloads[3];

	wire [RX_DELAY_BITS-1:0] rx_timestamps0 = tester.rx_timestamps[0];
	wire [RX_DELAY_BITS-1:0] rx_timestamps1 = tester.rx_timestamps[1];
	wire [RX_DELAY_BITS-1:0] rx_timestamps2 = tester.rx_timestamps[2];
	wire [RX_DELAY_BITS-1:0] rx_timestamps3 = tester.rx_timestamps[3];

endmodule
