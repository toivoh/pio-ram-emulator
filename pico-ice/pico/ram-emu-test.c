#include <stdlib.h>
#include "pico/stdio.h"
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "ice_usb.h"
#include "ice_fpga.h"
#include "ice_led.h"
#include "ice_led.h"
#include <tusb.h>

#include "../../ram-emu.h"
#include "build/serial-ram-emu.pio.h"



#define ADDRESS_PIN_BASE 0
#define DATA_PIN_BASE 4

#define RUN_MODE_PIN 14

#define SB_IN_PIN_BASE 0
#define SB_OUT_PIN_BASE 4


#if FPGA_CLOCK_PIN != ICE_FPGA_CLOCK_PIN
#error "FPGA_CLOCK_PIN (from pio file) != ICE_FPGA_CLOCK_PIN (from pico_ice.h)"
#endif


enum { TX_HEADER_BITS = 4 };
enum { TX_HEADER_TRANSCOUNT = 0, TX_HEADER_ADDR = 1, TX_HEADER_DATA = 4, TX_HEADER_NONE = 5 };

enum { RX_CFG_BITS_PER_MSG = 14, MSG_INDEX_BITS = 10, MAX_TX_BITS = 24, MAX_MESSAGES = 1024 };


bool sb_serial_in_on = false;
PSM sb_serial_in_psm;


static void init(bool enable_sb_in) {
	// Initialize PLL, USB, ...
	// ========================
	set_sys_clock_pll(1512 * MHZ, 5, 6); //  50.4 MHz for RP2040, half for FPGA

	tusb_init();
	stdio_init_all();

	ice_usb_init();

	// Set up run mode pin
	// ===================
	gpio_init(RUN_MODE_PIN);
	gpio_set_dir(RUN_MODE_PIN, true);
	gpio_put(RUN_MODE_PIN, false);

	// Set up FPGA clock -- start before RAM emulator
	// ==============================================
	gpio_set_function(ICE_FPGA_CLOCK_PIN, GPIO_FUNC_PWM);
	uint fpga_clock_slice_num = pwm_gpio_to_slice_num(ICE_FPGA_CLOCK_PIN);

	// Period 2, one cycle low and one cycle high
	pwm_set_wrap(fpga_clock_slice_num, 1);
	pwm_set_chan_level(fpga_clock_slice_num, ICE_FPGA_CLOCK_PIN & 1, 1);
	// The clock doesn't start until the pwm is enabled

	// Start the FPGA
	// ==============
	//clock_gpio_init(ICE_FPGA_CLOCK_PIN, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_SYS, 2);
	ice_fpga_start();
	// Enable the PWM, starts the FPGA clock
	pwm_set_enabled(fpga_clock_slice_num, true);

	// Set up the RAM emulator
	// =======================
	bool ok = ram_emu_init(SB_IN_PIN_BASE, SB_OUT_PIN_BASE, false);

	// Start PIO
	// =========
	PIO pio = pio1;
	PSM *psm;

	// SB serial in
	// ------------
	sb_serial_in_on = enable_sb_in;
	if (enable_sb_in) {
		psm = &sb_serial_in_psm;
		if (add_psm(psm, pio, &sb_serial_in2_program)) sb_serial_in2_program_init(pio, psm->sm, psm->offset, SB_IN_PIN_BASE); else ok = false;
	}

	// Check that it worked
	// --------------------
	if (!ok) {
		while (true) {
			tud_task();
			printf("PIO init failed!\r\n");
		}
	}
}

// Caller should add start bit manually
void sbio2_send_raw(uint data_out) {
	pio_sm_put_blocking(pio0, tx_rdata_psm.sm, data_out);
}

uint sbio2_receive() {
	return pio_sm_get_blocking(rx_wdata_psm.pio, rx_wdata_psm.sm);
}


enum {
	RX_DATA_CYCLES = 8,
	CFG_HEADER_BITS = 4,

	IO_BITS = SBIO2_NUM_PINS,
	RX_DATA_BITS = IO_BITS * RX_DATA_CYCLES,
	CFG_DATA_BITS = RX_DATA_BITS - CFG_HEADER_BITS
};

enum {
	CFG_HEADER_SET_TX_INDEX = 0,
	CFG_HEADER_SET_TX_STOP_INDEX = 1,
	CFG_HEADER_SET_TX_COUNTDOWN = 2,
	CFG_HEADER_SET_INDEX = 3,
	CFG_HEADER_SET_DELAY = 4,
	CFG_HEADER_SET_COUNT = 5,
	CFG_HEADER_SET_PAYLOAD0 = 6,
	CFG_HEADER_SET_PAYLOAD1 = 7,
	CFG_HEADER_SET_RX_INDEX = 8,
	CFG_HEADER_READ_RX_INDEX = 9,
	CFG_HEADER_READ_RX_PAYLOAD = 10,
	CFG_HEADER_READ_RX_TIMESTAMP = 11
};

void send_cfgmode_msg(uint header, uint data) {
	uint payload = (header & ((1 << CFG_HEADER_BITS) - 1)) | (data << CFG_HEADER_BITS);
	sbio2_send_raw(payload);
}


void send_cfgmode_set_txcfg(uint index, uint stop_index, uint countdown, uint rx_index) {
	send_cfgmode_msg(CFG_HEADER_SET_TX_INDEX, index);
	send_cfgmode_msg(CFG_HEADER_SET_TX_STOP_INDEX, stop_index);
	send_cfgmode_msg(CFG_HEADER_SET_TX_COUNTDOWN, countdown);
	send_cfgmode_msg(CFG_HEADER_SET_RX_INDEX, rx_index);
}

void send_cfgmode_set_txmsg(uint index, uint payload, uint count, uint delay) {
	send_cfgmode_msg(CFG_HEADER_SET_INDEX, index);
	send_cfgmode_msg(CFG_HEADER_SET_DELAY, delay);
	send_cfgmode_msg(CFG_HEADER_SET_COUNT, count);
	send_cfgmode_msg(CFG_HEADER_SET_PAYLOAD0, payload);
	send_cfgmode_msg(CFG_HEADER_SET_PAYLOAD1, payload >> CFG_DATA_BITS);
}

void set_txmsg_wcount(uint index, uint count)   { send_cfgmode_set_txmsg(index, (count   << TX_HEADER_BITS) | TX_HEADER_TRANSCOUNT | (TX_HEADER_NONE << 1), 10, 1); }
void set_txmsg_waddr( uint index, uint address) { send_cfgmode_set_txmsg(index, (address << TX_HEADER_BITS) | TX_HEADER_ADDR       | (TX_HEADER_NONE << 1), 10, 1); }
void set_txmsg_wdata( uint index, uint data)    { send_cfgmode_set_txmsg(index, (data    << TX_HEADER_BITS) | TX_HEADER_DATA       | (TX_HEADER_NONE << 1), 10, 1); }

void set_txmsg_rcount(uint index, uint count)   { send_cfgmode_set_txmsg(index, (count   << TX_HEADER_BITS) | TX_HEADER_NONE | (TX_HEADER_TRANSCOUNT << 1), 10, 1); }
void set_txmsg_raddr( uint index, uint address, uint extra_delay) { send_cfgmode_set_txmsg(index, (address << TX_HEADER_BITS) | TX_HEADER_NONE | (TX_HEADER_ADDR       << 1), 10, 1 + extra_delay); }


void send_cfgmode_read_rxcfg() {
	send_cfgmode_msg(CFG_HEADER_READ_RX_INDEX, 0);
}

void send_cfgmode_read_rxpayload(uint index) {
	send_cfgmode_msg(CFG_HEADER_READ_RX_PAYLOAD, index);
}

void send_cfgmode_read_rxtimestamp(uint index) {
	send_cfgmode_msg(CFG_HEADER_READ_RX_TIMESTAMP, index);
}


enum { PRINT_FLAGS_ALL = 63, PRINT_FLAGS_SB_IN = 1 };

int print_rx_fifo_data_tud_task(int flags) {
	int value = -1;
	tud_task();

	if (sb_serial_in_on && (flags & 1) && !pio_sm_is_rx_fifo_empty(sb_serial_in_psm.pio, sb_serial_in_psm.sm)) {
		uint data = pio_sm_get(sb_serial_in_psm.pio, sb_serial_in_psm.sm);
		printf("Received sb data = 0x%x\r\n", data);
		tud_task();
	}

	if ((flags & 2) && !pio_sm_is_rx_fifo_empty(rx_wdata_psm.pio, rx_wdata_psm.sm)) {
		uint data = pio_sm_get(rx_wdata_psm.pio, rx_wdata_psm.sm);
		value = data;
		printf("Received rx_wdata = 0x%x\r\n", data);
		tud_task();
	}

	if ((flags & 4) && !pio_sm_is_rx_fifo_empty(rx_waddr_psm.pio, rx_waddr_psm.sm)) {
		uint data = pio_sm_get(rx_waddr_psm.pio, rx_waddr_psm.sm);
		printf("Received rx_waddr = 0x%x\r\n", data);
		tud_task();
	}

	if ((flags & 8) && !pio_sm_is_rx_fifo_empty(rx_wcount_psm.pio, rx_wcount_psm.sm)) {
		uint data = pio_sm_get(rx_wcount_psm.pio, rx_wcount_psm.sm);
		printf("Received rx_wcount = 0x%x\r\n", data);
		tud_task();
	}

	if ((flags & 16) && !pio_sm_is_rx_fifo_empty(rx_raddr_psm.pio, rx_raddr_psm.sm)) {
		uint data = pio_sm_get(rx_raddr_psm.pio, rx_raddr_psm.sm);
		printf("Received rx_raddr = 0x%x\r\n", data);
		tud_task();
	}

	if ((flags & 32) && !pio_sm_is_rx_fifo_empty(rx_rcount_psm.pio, rx_rcount_psm.sm)) {
		uint data = pio_sm_get(rx_rcount_psm.pio, rx_rcount_psm.sm);
		printf("Received rx_rcount = 0x%x\r\n", data);
		tud_task();
	}

	return value;
}


int test_fpga2() {
	init(true);

/*
	uint16_t payloads[] = {
		(RX_CFG_HEADER_READ_RXCFG << SBIO2_NUM_PINS) | 1,
		0x0001, 0x0200, 0x0000, 0x0000,
		0x0005, 0x0080, 0x6814, 0x0d8c, 0x0000,
		0x0025, 0x0100, 0x2814, 0x58d8, 0x0000,
		0x0045, 0x0180, 0x6814, 0x0d8c, 0x0000,
		0x0065, 0x0200, 0x2814, 0x58d8, 0x0000,
	};
	const int num_payloads = sizeof(payloads)/sizeof(uint16_t);
*/

	bool run_mode = false;
	uint64_t last_time = 0;
//	int counter = 0;
	while (true) {
		tud_task();

		if (sb_serial_in_on && !pio_sm_is_rx_fifo_empty(sb_serial_in_psm.pio, sb_serial_in_psm.sm)) {
			uint data = pio_sm_get(sb_serial_in_psm.pio, sb_serial_in_psm.sm);
			printf("Received sb data = 0x%x\r\n", data);
		}

		if (!pio_sm_is_rx_fifo_empty(rx_wdata_psm.pio, rx_wdata_psm.sm)) {
			uint data = pio_sm_get(rx_wdata_psm.pio, rx_wdata_psm.sm);
			printf("Received rx_wdata = 0x%x\r\n", data);
		}

		if (!pio_sm_is_rx_fifo_empty(rx_waddr_psm.pio, rx_waddr_psm.sm)) {
			uint data = pio_sm_get(rx_waddr_psm.pio, rx_waddr_psm.sm);
			printf("Received rx_waddr = 0x%x\r\n", data);
		}

		uint64_t time = time_us_64();
		bool step = (last_time & ~((1 << 16) - 1)) != (time & ~((1 << 16) - 1));
		if (step) {
			run_mode = !run_mode;
			gpio_put(RUN_MODE_PIN, run_mode);
			printf("Set run_mode = %d\r\n", run_mode);

			if (run_mode == false) {
				send_cfgmode_read_rxcfg();

				int num_tx_msgs = 4;

				send_cfgmode_set_txcfg(0, num_tx_msgs, 0, 0);
				for (int i = 0; i < num_tx_msgs; i++) {
					uint payload = ((0x1234 + i*0x1111) << TX_HEADER_BITS) | TX_HEADER_DATA;
					send_cfgmode_set_txmsg(i, payload, 10, i + 1);
				}
			}

/*
			if (run_mode || counter >= num_payloads) {
				run_mode = !run_mode;
				gpio_put(RUN_MODE_PIN, run_mode);
				printf("Set run_mode = %d\r\n", run_mode);
				counter = 0;
			} else {
				uint16_t data = payloads[counter];
				sbio2_send_raw(data);
				printf("Sent 0x%x\r\n", data);
				counter++;
			}
*/
			//printf("GPIO = 0x%x\r\n", (int)(sio_hw->gpio_in));
		}
		last_time = time;
	}
}

int test_fpga3() {
	init(true);

	bool run_mode = false;

	uint64_t last_time = 0;
	while (true) {
		run_mode = false;
		gpio_put(RUN_MODE_PIN, run_mode);
		printf("Set run_mode = %d\r\n", run_mode);

		send_cfgmode_read_rxcfg();

		int num_tx_msgs = 4;

		send_cfgmode_set_txcfg(0, num_tx_msgs, 0, 0);
		for (int i = 0; i < num_tx_msgs; i++) {
			uint payload = ((0x1234 + i*0x1111) << TX_HEADER_BITS) | TX_HEADER_DATA;
			send_cfgmode_set_txmsg(i, payload, 10, i + 1);
		}
		printf("Sent cfg mode data\r\n");

		int rx_index = 0;

		// wait
		uint64_t next_time = time_us_64() + 65536;
		//print_rx_fifo_data_tud_task(PRINT_FLAGS_ALL);
		//while (time_us_64() < next_time) print_rx_fifo_data_tud_task(PRINT_FLAGS_ALL);
		bool received_rx_index = false;
		while (true) {
			int data = print_rx_fifo_data_tud_task(PRINT_FLAGS_ALL);
			if (!received_rx_index && data != -1) {
				int new_rx_index = data;
				received_rx_index = true;
				printf("rx_index = %d\r\n", new_rx_index); tud_task();

				while (rx_index != new_rx_index) {
				//while (rx_index != ((new_rx_index+1)&((1 << MSG_INDEX_BITS) - 1))) {
					printf("Sent read for payload and timestamp of rx_index = %d\r\n", rx_index);
					send_cfgmode_read_rxpayload(rx_index);
					send_cfgmode_read_rxtimestamp(rx_index);
					rx_index = (rx_index + 1) & ((1 << MSG_INDEX_BITS) - 1);
				}
				//rx_index = new_rx_index;
			}
			if (time_us_64() >= next_time) break;
		}

		run_mode = true;
		gpio_put(RUN_MODE_PIN, run_mode);
		printf("Set run_mode = %d\r\n", run_mode);

		// wait
		next_time = time_us_64() + 65536;
		print_rx_fifo_data_tud_task(PRINT_FLAGS_ALL);
		while (time_us_64() < next_time) print_rx_fifo_data_tud_task(PRINT_FLAGS_ALL);
	}
}



int test_dma1() {
	const bool do_dma = true;
	const int pflags = do_dma ? PRINT_FLAGS_SB_IN : PRINT_FLAGS_ALL;

	init(true);
	//if (do_dma) init_dma();

	bool run_mode = false;
	gpio_put(RUN_MODE_PIN, run_mode);

	uint64_t last_time = 0;
	while (true) {
		if (do_dma) memset(emu_ram, 0, sizeof(emu_ram));

		send_cfgmode_read_rxcfg();

		/*
		int num_tx_msgs = 4;

		send_cfgmode_set_txcfg(0, num_tx_msgs, 0, 0);
		for (int i = 0; i < num_tx_msgs; i++) {
			uint payload = ((0x1234 + i*0x1111) << TX_HEADER_BITS) | TX_HEADER_DATA;
			send_cfgmode_set_txmsg(i, payload, 10, i + 1);
		}
		*/
		int num_tx_msgs = 0;
		int wait_cycles = 0;
		for (int i = 0; i < 2; i++) {
			set_txmsg_waddr(num_tx_msgs++, i*2+1); wait_cycles += 12;
			set_txmsg_wdata(num_tx_msgs++, 0x1234 + i*0x1111); wait_cycles += 12;
		}
		send_cfgmode_set_txcfg(0, num_tx_msgs, 0, 0);

		printf("Sent cfg mode data\r\n");


		int rx_index = 0;

		// wait
		uint64_t next_time = time_us_64() + 65536;
		//print_rx_fifo_data_tud_task(PRINT_FLAGS_ALL);
		//while (time_us_64() < next_time) print_rx_fifo_data_tud_task(PRINT_FLAGS_ALL);
		bool received_rx_index = false;
		while (true) {
			int data = print_rx_fifo_data_tud_task(PRINT_FLAGS_ALL);
			if (!received_rx_index && data != -1) {
				int new_rx_index = data;
				received_rx_index = true;
				printf("rx_index = %d\r\n", new_rx_index); tud_task();

				while (rx_index != new_rx_index) {
				//while (rx_index != ((new_rx_index+1)&((1 << MSG_INDEX_BITS) - 1))) {
					printf("Sent read for payload and timestamp of rx_index = %d\r\n", rx_index);
					send_cfgmode_read_rxpayload(rx_index);
					send_cfgmode_read_rxtimestamp(rx_index);
					rx_index = (rx_index + 1) & ((1 << MSG_INDEX_BITS) - 1);
				}
				//rx_index = new_rx_index;
			}
			if (time_us_64() >= next_time) break;
		}

		// Enable DMA and start run mode
		if (do_dma) ram_emu_configure_dma(true);
		run_mode = true;
		gpio_put(RUN_MODE_PIN, run_mode);
		printf("Set run_mode = %d\r\n", run_mode);

		// wait
		next_time = time_us_64() + 65536;
		print_rx_fifo_data_tud_task(pflags);
		while (time_us_64() < next_time) print_rx_fifo_data_tud_task(pflags);

		// Stop DMA and go back to cfg mode
		if (do_dma) {
			ram_emu_stop_dma();
			ram_emu_configure_dma(false);
		}
		run_mode = false;
		gpio_put(RUN_MODE_PIN, run_mode);
		printf("Set run_mode = %d\r\n", run_mode);

		if (do_dma) {
			// Read out results
			printf("\r\rRAM after writes:\r\n");
			for (int i = 0; i < 10; i++) {
				printf("ram[%d] = 0x%x\r\n", i, emu_ram[i]);
			}
		}
	}
}

int test_dma2() {
	const bool do_dma = true;
	const int pflags = do_dma ? PRINT_FLAGS_SB_IN : PRINT_FLAGS_ALL;

	init(true);
	//if (do_dma) init_dma();

	bool run_mode = false;
	gpio_put(RUN_MODE_PIN, run_mode);

	uint64_t last_time = 0;
	while (true) {
		if (do_dma) memset(emu_ram, 0, sizeof(emu_ram));

		send_cfgmode_read_rxcfg();

		/*
		int num_tx_msgs = 4;

		send_cfgmode_set_txcfg(0, num_tx_msgs, 0, 0);
		for (int i = 0; i < num_tx_msgs; i++) {
			uint payload = ((0x1234 + i*0x1111) << TX_HEADER_BITS) | TX_HEADER_DATA;
			send_cfgmode_set_txmsg(i, payload, 10, i + 1);
		}
		*/
		int num_tx_msgs = 0;
		int wait_cycles = 0;
		for (int i = 0; i < 2; i++) {
			int count = i + 1;
			set_txmsg_wcount(num_tx_msgs++, count); wait_cycles += 12;
			set_txmsg_waddr(num_tx_msgs++, i*4+1); wait_cycles += 12;
			for (int j=0; j < count; j++) {
				set_txmsg_wdata(num_tx_msgs++, 0x1234 + (i*2 + j)*0x1111); wait_cycles += 12;
			}
		}

		set_txmsg_rcount(num_tx_msgs++, 6);
		set_txmsg_raddr(num_tx_msgs++, 1, 0);

		send_cfgmode_set_txcfg(0, num_tx_msgs, 0, 0);


		printf("Sent cfg mode data\r\n");


		int rx_index = 0;

		// wait
		uint64_t next_time = time_us_64() + 65536;
		//print_rx_fifo_data_tud_task(PRINT_FLAGS_ALL);
		//while (time_us_64() < next_time) print_rx_fifo_data_tud_task(PRINT_FLAGS_ALL);
		bool received_rx_index = false;
		while (true) {
			int data = print_rx_fifo_data_tud_task(PRINT_FLAGS_ALL);
			if (!received_rx_index && data != -1) {
				int new_rx_index = data;
				received_rx_index = true;
				printf("rx_index = %d\r\n", new_rx_index); tud_task();

				while (rx_index != new_rx_index) {
				//while (rx_index != ((new_rx_index+1)&((1 << MSG_INDEX_BITS) - 1))) {
					send_cfgmode_read_rxpayload(rx_index);
					send_cfgmode_read_rxtimestamp(rx_index);
					printf("Sent read for payload and timestamp of rx_index = %d\r\n", rx_index);
					print_rx_fifo_data_tud_task(PRINT_FLAGS_ALL); print_rx_fifo_data_tud_task(PRINT_FLAGS_ALL);
					rx_index = (rx_index + 1) & ((1 << MSG_INDEX_BITS) - 1);
				}
				//rx_index = new_rx_index;
			}
			if (time_us_64() >= next_time) break;
		}

		// Enable DMA and start run mode
		if (do_dma) ram_emu_configure_dma(true);
		run_mode = true;
		gpio_put(RUN_MODE_PIN, run_mode);
		printf("Set run_mode = %d\r\n", run_mode);

		// wait
		next_time = time_us_64() + 65536;
		print_rx_fifo_data_tud_task(pflags);
		while (time_us_64() < next_time) print_rx_fifo_data_tud_task(pflags);

		// Stop DMA and go back to cfg mode
		if (do_dma) {
			ram_emu_stop_dma();
			ram_emu_configure_dma(false);
		}
		run_mode = false;
		gpio_put(RUN_MODE_PIN, run_mode);
		printf("Set run_mode = %d\r\n", run_mode);

		if (do_dma) {
			// Read out results
			printf("\r\rRAM after writes:\r\n");
			for (int i = 0; i < 10; i++) {
				printf("ram[%d] = 0x%x\r\n", i, emu_ram[i]);
			}
		}
	}
}

int test_read_dma() {
	const bool do_dma = true;
	const int pflags = do_dma ? PRINT_FLAGS_SB_IN : PRINT_FLAGS_ALL;

	for (int i = 0; i < emu_ram_elements; i++) emu_ram[i] = i;

	init(false);
	//if (do_dma) init_dma();

	bool run_mode = false;
	gpio_put(RUN_MODE_PIN, run_mode);

	uint64_t last_time = 0;
	while (true) {

		const int RCOUNT = 2;
		const int NUM_READS = MAX_MESSAGES/2 - 1;
		const int EXPECTED_RX_MSGS = RCOUNT * NUM_READS;

		int num_tx_msgs = 0;
		int wait_cycles = 0;

		set_txmsg_rcount(num_tx_msgs++, RCOUNT); wait_cycles += 12;
		for (int i = 0; i < NUM_READS; i++) {
			set_txmsg_raddr(num_tx_msgs++, i*(RCOUNT + 1), 12); wait_cycles += 24;
		}

		send_cfgmode_set_txcfg(0, num_tx_msgs, 0, 0);
		printf("Sent cfg mode data\r\n");

		// wait
		uint64_t next_time = time_us_64() + 65536;
		print_rx_fifo_data_tud_task(PRINT_FLAGS_ALL);
		while (time_us_64() < next_time) print_rx_fifo_data_tud_task(PRINT_FLAGS_ALL);

		// Enable DMA and start run mode
		if (do_dma) ram_emu_configure_dma(true);
		run_mode = true;
		gpio_put(RUN_MODE_PIN, run_mode);
		printf("Set run_mode = %d\r\n", run_mode);

		// wait
		next_time = time_us_64() + 65536;
		print_rx_fifo_data_tud_task(pflags);
		while (time_us_64() < next_time) print_rx_fifo_data_tud_task(pflags);

		// Stop DMA and go back to cfg mode
		if (do_dma) {
			ram_emu_stop_dma();
			ram_emu_configure_dma(false);
		}
		run_mode = false;
		gpio_put(RUN_MODE_PIN, run_mode);
		printf("Set run_mode = %d\r\n", run_mode);


		int num_errors = 0;

		send_cfgmode_read_rxcfg();
		int final_rx_index = sbio2_receive();
		printf("rx_index = %d\r\n", final_rx_index);
		if (final_rx_index != EXPECTED_RX_MSGS) {
			num_errors++;
			printf("Expected rx_index = %d instead! ****\r\n", EXPECTED_RX_MSGS);
		}

		int num_rx_msgs = (final_rx_index < EXPECTED_RX_MSGS ? final_rx_index : EXPECTED_RX_MSGS);
		int num_reads = num_rx_msgs / RCOUNT;

		int rx_index = 0;
		int first_timestamp = -1;
		int prev_timestamp;
		int min_delay = 0xffff;
		int max_delay = 0;
		for (int j = 0; j < num_reads; j++) {
			for (int i = 0; i < RCOUNT; i++) {
				send_cfgmode_read_rxpayload(rx_index);
				send_cfgmode_read_rxtimestamp(rx_index);
				int payload = sbio2_receive();
				int timestamp = sbio2_receive();
				print_rx_fifo_data_tud_task(PRINT_FLAGS_ALL);

				int expected_payload = j*(RCOUNT + 1) + i;
				if (payload != expected_payload) {
					num_errors++;
					printf("(%d, %d): Expected payload = %d, got %d! ****", j, i, expected_payload, payload);
				}

				if (first_timestamp == -1) first_timestamp = timestamp;
				else {
					int delay = timestamp - prev_timestamp;
					if (delay < min_delay) min_delay = delay;
					if (delay > max_delay) max_delay = delay;
				}
				prev_timestamp = timestamp;

				rx_index++;

				if (num_errors >= 10) break;
			}
			if (num_errors >= 10) break;
		}

		printf("First timestamp = %d, %d <= delay <= %d\r\n", first_timestamp, min_delay, max_delay);

		if (num_errors > 0) printf("%d errors found! **************************************************************\r\n", num_errors);
	}
}


int main(void) {
	//return test_fpga2();
	//return test_fpga3();
	//return test_dma();

	//return test_dma1();
	//return test_dma2();

	return test_read_dma();
};
