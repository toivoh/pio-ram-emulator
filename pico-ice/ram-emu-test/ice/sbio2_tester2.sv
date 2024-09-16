`default_nettype none

module input_pins #( parameter BITS=1 ) (
		input wire clk,
		input wire [BITS-1:0] pins,
		output wire [BITS-1:0] data
	);

	generate
		genvar i;
		for (i = 0; i < BITS; i++) begin : pin
			// Registered input
			SB_IO #(.PIN_TYPE(6'b000000)) io_pin(
				.PACKAGE_PIN(pins[i]),
				.INPUT_CLK(clk),
				.OUTPUT_CLK(clk),
				.D_IN_0(data[i])
			);
		end
	endgenerate
endmodule

module output_pins #( parameter BITS=1 ) (
		input wire clk,
		input wire [BITS-1:0] data,
		output wire [BITS-1:0] pins
	);

	generate
		genvar i;
		for (i = 0; i < BITS; i++) begin : pin
			// Registered output
			SB_IO #(.PIN_TYPE(6'b010100)) io_pin(
				.PACKAGE_PIN(pins[i]),
				.INPUT_CLK(clk),
				.OUTPUT_CLK(clk),
				.D_OUT_0(data[i])
				//.D_OUT_1(data[i]) // Shouldn't be needed since this is not configured as a DDR pin? Gives timing problems when used.
			);
		end
	endgenerate
endmodule


`define TX_HEADER_TRANSCOUNT 4'd0
`define TX_HEADER_ADDR       4'd1
`define TX_HEADER_DATA       4'd4
`define TX_HEADER_NONE       4'd5

module sbio2_tester2 #( parameter IO_BITS=2, RX_HEADER_CYCLES=2, RX_DATA_CYCLES=8 ) (
		input wire clk, reset,

		input wire run_mode, // When high, run the tests that have been set up.

		input wire [IO_BITS-1:0] rx_pins,
		output wire [IO_BITS-1:0] tx_pins
	);
	localparam MAX_TX_CYCLES = 12;
	localparam MAX_MESSAGES = 1024;
	localparam TX_DELAY_BITS = 8;

	localparam SBIO_COUNTER_BITS = 6;

	localparam TX_HEADER_BITS = 4;


	localparam CFG_HEADER_BITS = 4;
	localparam CFG_HEADER_SET_TX_INDEX = 0;
	localparam CFG_HEADER_SET_TX_STOP_INDEX = 1;
	localparam CFG_HEADER_SET_TX_COUNTDOWN = 2;
	localparam CFG_HEADER_SET_INDEX = 3;
	localparam CFG_HEADER_SET_DELAY = 4;
	localparam CFG_HEADER_SET_COUNT = 5;
	localparam CFG_HEADER_SET_PAYLOAD0 = 6;
	localparam CFG_HEADER_SET_PAYLOAD1 = 7;
	localparam CFG_HEADER_SET_RX_INDEX = 8;
	localparam CFG_HEADER_READ_RX_INDEX = 9;
	localparam CFG_HEADER_READ_RX_PAYLOAD = 10;
	localparam CFG_HEADER_READ_RX_TIMESTAMP = 11;


	localparam MSG_INDEX_BITS = $clog2(MAX_MESSAGES);

	localparam TX_COUNT_BITS = $clog2(MAX_TX_CYCLES);
	localparam MAX_TX_BITS = IO_BITS * MAX_TX_CYCLES;

	localparam RX_DATA_BITS = IO_BITS * RX_DATA_CYCLES;
	localparam RX_TIMESTAMP_BITS = RX_DATA_BITS;

	localparam CFG_DATA_BITS = RX_DATA_BITS - CFG_HEADER_BITS;


	wire set_cfg;

	// TX channel
	// ==========
	wire tx_reload_runmode;
	wire [MAX_TX_BITS-1:0] next_tx_sreg_runmode;
	wire [TX_COUNT_BITS-1:0] next_tx_count_runmode;

	reg tx_reload_cfgmode; // not a register
	wire [MAX_TX_BITS-1:0] next_tx_sreg_cfgmode;
	wire [TX_COUNT_BITS-1:0] next_tx_count_cfgmode = RX_DATA_CYCLES + RX_HEADER_CYCLES; // TODO: correct parameters?

	wire tx_reload  = run_mode ? tx_reload_runmode : tx_reload_cfgmode;
	wire [MAX_TX_BITS-1:0] next_tx_sreg = run_mode ? next_tx_sreg_runmode : next_tx_sreg_cfgmode;
	wire [TX_COUNT_BITS-1:0] next_tx_count = run_mode ? next_tx_count_runmode : next_tx_count_cfgmode;

	// TX monitor
	// ----------
	// Sends variable length messages, start indicated on all pins
	wire tx_started, tx_active, tx_done;
	wire [SBIO_COUNTER_BITS-1:0] tx_counter;
//	sbio_monitor #(.IO_BITS(IO_BITS), .SENS_BITS(1), .COUNTER_BITS(SBIO_COUNTER_BITS), .INACTIVE_COUNTER_VALUE(2**SBIO_COUNTER_BITS-1)) tx_monitor(
	sbio_monitor #(.IO_BITS(IO_BITS), .SENS_BITS(1), .COUNTER_BITS(SBIO_COUNTER_BITS), .INACTIVE_COUNTER_VALUE(0)) tx_monitor(
		.clk(clk), .reset(reset),
		.pins(~tx_pins), // because of active low start bit
		.start(tx_started), .active(tx_active), .counter(tx_counter), .done(tx_done)
	);

	// TX shift register
	// -----------------
	reg [MAX_TX_BITS-1:0] tx_sreg;
	reg tx_sreg_valid;
	reg [TX_COUNT_BITS-1:0] tx_sreg_count;

	wire start_tx = !tx_active && tx_sreg_valid;

	/*
	reg [IO_BITS-1:0] tx_out; // not a register
	always_comb begin
		tx_out = '1; // Default: don't start
		if (start_tx) tx_out = 0; // Start bits
		else if (tx_active) tx_out = tx_sreg[IO_BITS-1:0];
	end
	assign tx_pins = tx_out;
	*/
	assign tx_pins = tx_active ? tx_sreg[IO_BITS-1:0] : (start_tx ? '0 : '1);

	always_ff @(posedge clk) begin
		if (reset) begin
			tx_sreg_valid <= 0;
		end else begin
			if (tx_reload) tx_sreg_valid <= 1;
			else if (tx_done) tx_sreg_valid <= 0;
		end

		if (tx_reload) begin
			tx_sreg <= next_tx_sreg;
			tx_sreg_count <= next_tx_count;
		end else if (tx_active) begin
			tx_sreg <= tx_sreg >> IO_BITS;
		end
	end

	assign tx_done = (tx_counter == tx_sreg_count);
	wire tx_busy = tx_sreg_valid && !tx_done; // high when sending tx message except during last cycle

	// TX memories
	// -----------

	wire [CFG_HEADER_BITS-1:0] cfg_header;
	wire [CFG_DATA_BITS-1:0] cfg_data;


	reg [MSG_INDEX_BITS-1:0] tx_index;
	reg [MSG_INDEX_BITS-1:0] tx_index_stop;
	reg [MAX_TX_BITS-1:0] tx_payloads[MAX_MESSAGES];
	reg [TX_COUNT_BITS-1:0] tx_counts[MAX_MESSAGES];
	reg [TX_DELAY_BITS-1:0] tx_delays[MAX_MESSAGES];

	reg [TX_DELAY_BITS-1:0] tx_countdown;

	wire tx_stop = (tx_index == tx_index_stop);
	wire tx_advance = !tx_busy && !tx_stop && (tx_countdown == 0);

	assign next_tx_sreg_runmode = tx_payloads[tx_index];
	assign next_tx_count_runmode = tx_counts[tx_index];
	assign tx_reload_runmode = run_mode && tx_advance;

	always_ff @(posedge clk) begin
		if (reset) begin
			tx_index <= 0;
			tx_countdown <= 0;
			tx_index_stop <= 0;
		end else begin
			if (run_mode) begin
				if (tx_advance) begin
					tx_countdown <= tx_delays[tx_index];
					tx_index <= tx_index + 1;
				end else if (!tx_busy && !tx_stop) begin
					tx_countdown <= tx_countdown - 1;
				end
			end else begin
				if (set_cfg) begin
					if (cfg_header == CFG_HEADER_SET_TX_INDEX) tx_index <= cfg_data;
					if (cfg_header == CFG_HEADER_SET_TX_STOP_INDEX) tx_index_stop <= cfg_data;
					if (cfg_header == CFG_HEADER_SET_TX_COUNTDOWN) tx_countdown <= cfg_data;
				end
			end
		end
	end


	// RX channel
	// ==========

	// RX channel
	// ----------
	// Receives fixed length messages
	wire rx_started, rx_active, rx_done;
	wire [SBIO_COUNTER_BITS-1:0] rx_counter;
	sbio_monitor #(.IO_BITS(IO_BITS), .SENS_BITS(1), .COUNTER_BITS(SBIO_COUNTER_BITS), .INACTIVE_COUNTER_VALUE(2**SBIO_COUNTER_BITS-1-RX_HEADER_CYCLES)) rx_monitor(
		.clk(clk), .reset(reset),
		.pins(~rx_pins), // because of active low start bit
		.start(rx_started), .active(rx_active), .counter(rx_counter), .done(rx_done)
	);

	assign rx_done = (rx_counter == RX_DATA_CYCLES - 1);
	wire rx_receiving_data = rx_counter[SBIO_COUNTER_BITS-1] == 0;

	// RX shift register
	// -----------------
	reg [RX_DATA_BITS-1:0] rx_sreg;
	reg rx_sreg_valid, rx_sreg_valid2;

	always_ff @(posedge clk) begin
		if (rx_receiving_data) rx_sreg <= {rx_pins, rx_sreg[RX_DATA_BITS-1:IO_BITS]};
		rx_sreg_valid <= rx_done; // only valid for one cycle
		rx_sreg_valid2 <= rx_sreg_valid; // valid for one cycle after that
	end

	// RX memories
	// -----------

	reg [RX_DATA_BITS-1:0] rx_index_full;
	wire [MSG_INDEX_BITS-1:0] rx_index = rx_index_full[MSG_INDEX_BITS-1:0];


	reg [RX_DATA_BITS-1:0] rx_payloads[MAX_MESSAGES];
	reg [RX_TIMESTAMP_BITS-1:0] rx_timestamps[MAX_MESSAGES];

	// synchronous readout
	wire [MSG_INDEX_BITS-1:0] rx_msg_index;
	reg [RX_DATA_BITS-1:0] curr_rx_payload;
	reg [RX_TIMESTAMP_BITS-1:0] curr_rx_timestamp;
	always_ff @(posedge clk) begin
		curr_rx_payload <= rx_payloads[rx_msg_index];
		curr_rx_timestamp <= rx_timestamps[rx_msg_index];
	end

	reg [RX_TIMESTAMP_BITS-1:0] rx_timestamp;

	always @(posedge clk) begin
		if (reset) begin
			rx_index_full <= 0;
		end else begin
			if (run_mode) begin
				if (rx_sreg_valid) begin
					rx_payloads[rx_index] <= rx_sreg;
					rx_timestamps[rx_index] <= rx_timestamp;
					rx_index_full <= rx_index_full + 1;
				end
			end else begin
				if (set_cfg && cfg_header == CFG_HEADER_SET_RX_INDEX) rx_index_full <= cfg_data;
			end
		end

		if (reset || !run_mode) begin
			// Compensate for capture delay
			rx_timestamp <= -(1 + RX_HEADER_CYCLES + RX_DATA_CYCLES) - 1;
		end else begin
			rx_timestamp <= rx_timestamp + 1;
		end
	end

	// Cfg mode
	// --------
	wire cfg_mode = !run_mode;

	assign {cfg_data, cfg_header} = rx_sreg;
	assign rx_msg_index = cfg_data;

	assign set_cfg = cfg_mode && rx_sreg_valid2; // use rx_sreg_valid2 to give time for synchronous readout

	reg [MSG_INDEX_BITS-1:0] msg_index;
	reg [CFG_DATA_BITS-1:0] cfg_data_saved;

	always_ff @(posedge clk) begin
		if (set_cfg) begin
			case(cfg_header)
				CFG_HEADER_SET_INDEX: msg_index <= cfg_data;
				CFG_HEADER_SET_DELAY: tx_delays[msg_index] <= cfg_data;
				CFG_HEADER_SET_COUNT: tx_counts[msg_index] <= cfg_data;
				CFG_HEADER_SET_PAYLOAD0: cfg_data_saved <= cfg_data;
				CFG_HEADER_SET_PAYLOAD1: tx_payloads[msg_index] <= {cfg_data, cfg_data_saved};
			endcase
		end
	end

	reg [RX_DATA_BITS-1:0] next_tx_data_cfgmode; // not a register
	always_comb begin
		next_tx_data_cfgmode = 'X;
		tx_reload_cfgmode = 0;
		if (set_cfg) begin
			case(cfg_header)
				CFG_HEADER_READ_RX_INDEX:     begin tx_reload_cfgmode = 1; next_tx_data_cfgmode = rx_index_full; end
				CFG_HEADER_READ_RX_PAYLOAD:   begin tx_reload_cfgmode = 1; next_tx_data_cfgmode = curr_rx_payload; end
				CFG_HEADER_READ_RX_TIMESTAMP: begin tx_reload_cfgmode = 1; next_tx_data_cfgmode = curr_rx_timestamp; end
			endcase
		end
	end
	wire [TX_HEADER_BITS-1:0] next_tx_header_cfgmode = `TX_HEADER_DATA | (`TX_HEADER_NONE << 1);
	assign next_tx_sreg_cfgmode = {next_tx_data_cfgmode, next_tx_header_cfgmode};

endmodule


module top #( parameter IO_BITS=2, RX_HEADER_CYCLES=2, RX_DATA_CYCLES=8 ) (
		input clk,

		output led_red,
		output led_green,
		output led_blue,

		output tx_pin0, tx_pin1,
		input  rx_pin0, rx_pin1,

		input mode_pin
	);

	wire [IO_BITS-1:0] tx_pins, rx_pins;
	wire run_mode;
	output_pins #(.BITS(IO_BITS)) serial_output_pins(.clk(clk), .data(tx_pins),  .pins({tx_pin1, tx_pin0}));
	input_pins  #(.BITS(IO_BITS))  serial_input_pins(.clk(clk), .data(rx_pins),  .pins({rx_pin1, rx_pin0}));
	input_pins  #(.BITS(1))          mode_input_pins(.clk(clk), .data(run_mode), .pins(mode_pin));

	reg _reset = 1;
	always @(posedge clk) _reset <= 0;
	wire reset = _reset;

	sbio2_tester2 #( .IO_BITS(IO_BITS), .RX_HEADER_CYCLES(RX_HEADER_CYCLES), .RX_DATA_CYCLES(RX_DATA_CYCLES) ) tester(
		.clk(clk), .reset(reset),
		.run_mode(run_mode),
		.rx_pins(rx_pins), .tx_pins(tx_pins)
	);

	assign led_red = run_mode;
	assign led_green = !run_mode;
	assign led_blue = 0;
endmodule
