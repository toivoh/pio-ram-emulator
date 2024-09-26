pio-ram-emulator for Pico-Ice
=============================
This directory contains code (in `pico/`) to run the RAM emulator on the RP2040 of a [Pico-Ice](https://pico-ice.tinyvision.ai/).
https://github.com/toivoh/tt09-pio-ram-emulator-example/tree/main/pico-ice contains an example that can be run with the RAM emulator on a Pico-Ice.

To build, [pico/CMakeLists.txt](pico/CMakeLists.txt) expects two symlinks in the `pico/` directory, `pico-sdk` and `pico-ice-sdk`, pointing to the respective SDK.
In `pico/`, to build, do

	mkdir build
	cd build
	cmake ..
	make

The build will create `serial-ram-emu.pio.h`, which the code expects to be in `pico/build/`.

Assumptions
-----------
The RAM emulator will clock the FPGA at 50.4 MHz (good for VGA with 2 cycles per pixel).
You can uncomment `//#define HALF_FREQ` in `ram-emu-main.c` to reduce it to 25.2 MHz, or you can change the call to `set_sys_clock_pll` to change to another frequency if you know what you are doing.

The code makes assumptions about the pins used to communicate with the FPGA:
- The emulator's RX pins (the FPGAs TX pins) are RP0-1
- The emulator's TX pins (the FPGAs RX pins) are RP4-5
- The FPGA design has an active high RESET pin connected to RP14

These pin mappings can be changed by changing the defines `RX_PIN_BASE, TX_PIN_BASE`, and `RESET_PIN`, but the RX pins need to be consecutive for the RP2040, as do the TX pins.

The FPGA is started before the RAM emulator, but with the RESET pin held high until after the RAM emulator is started. The FPGA should hold its TX pins high (no start bit) during reset, so that the RAM emulator can be completely initialized before it needs to start handling traffic.
