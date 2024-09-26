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

#include "../../../ram-emu.h"
#include "build/serial-ram-emu.pio.h"


//#define HALF_FREQ


#define RESET_PIN 14

#define RX_PIN_BASE 0
#define TX_PIN_BASE 4


#if FPGA_CLOCK_PIN != ICE_FPGA_CLOCK_PIN
#error "FPGA_CLOCK_PIN (from pio file) != ICE_FPGA_CLOCK_PIN (from pico_ice.h)"
#endif


enum { TX_HEADER_BITS = 4 };
enum { TX_HEADER_TRANSCOUNT = 0, TX_HEADER_ADDR = 1, TX_HEADER_DATA = 4, TX_HEADER_NONE = 5 };

enum { RX_CFG_BITS_PER_MSG = 14, MSG_INDEX_BITS = 10, MAX_TX_BITS = 24, MAX_MESSAGES = 1024 };



static void init() {
	// Initialize PLL, USB, ...
	// ========================
#ifdef HALF_FREQ
	set_sys_clock_pll(1512 * MHZ, 5, 6); //  50.4 MHz for RP2040, half for FPGA
#else
	set_sys_clock_pll(1512 * MHZ, 5, 3); // 100.8 MHz for RP2040, half for FPGA
#endif

	tusb_init();
	stdio_init_all();

	ice_usb_init();

	// Set up reset pin, keep high for now
	// ===================================
	gpio_init(RESET_PIN);
	gpio_set_dir(RESET_PIN, true);
	gpio_put(RESET_PIN, true);

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
	// Enable the PWM, starts the FPGA clock (reset is still high)
	pwm_set_enabled(fpga_clock_slice_num, true);

	// Set up the RAM emulator
	// =======================
	bool ok = ram_emu_init(RX_PIN_BASE, TX_PIN_BASE, false);

	// Check that it worked
	// --------------------
	if (!ok) {
		while (true) {
			tud_task();
			printf("PIO init failed!\r\n");
		}
	}

	ram_emu_configure_dma(true);

	// Release reset
	// =============
	gpio_put(RESET_PIN, false);
}

int main(void) {

	// Initialization
	// ==============

	memset(emu_ram, 0, sizeof(emu_ram));

//	for (int i = 0; i < emu_ram_elements; i++) emu_ram[i] = i;
/*
	for (int j = 0; j < 480; j++) {
		for (int i = 0; i < 160/4; i++) {
			int pixels = 0;
			for (int k = 0; k < 4; k++) {
				int dx = 4*(i*4 + k) - 320;
				int dy = j - 240;

				int d2 = dx*dx + dy*dy;

				int c = (d2 >> 12) & 15;

				pixels += c << (k*4);
			}
			emu_ram[i+j*64] = pixels;
		}
	}
*/
/*
	for (int j = 0; j < 480; j++) {
		for (int i = 0; i < 320/8; i++) {
			int pixels = 0;
			for (int k = 0; k < 8; k++) {
				int dx = 2*(i*8 + k) - 320;
				int dy = j - 240;

				int d2 = dx*dx + dy*dy;

				int c = (d2 >> 12) & 15;

				pixels += c << (k*2);
			}
			emu_ram[i+j*64] = pixels;
		}
	}
*/

	init();

	// Main loop
	// =========

	uint64_t last_time = 0;
	while (true) {
		tud_task();

		uint64_t time = time_us_64();
		bool step = (last_time & ~((1 << 16) - 1)) != (time & ~((1 << 16) - 1));

		if (step) {
			printf("hello\r\n");
		}

		last_time = time;
	}
	return 0;
};
