This directory contains test code to test the RAM emulator on a [Pico-Ice](https://pico-ice.tinyvision.ai/):
- `ice/`: FPGA code to
	- set up a number of messages to the RAM emulator,
	- play them back with specified timing,
	- record the responses, and
	- let the RP2040 read back the responses and their timing
- `pico/`: C code for the RP2040 to
	- set up the RAM emulator, and
	- communicate with the FPGA code to test the RAM emulator

To build, [pico/CMakeLists.txt](pico/CMakeLists.txt) expects two symlinks in the `pico/` directory, `pico-sdk` and `pico-ice-sdk`, pointing to the respective SDK.
In `pico/`, to build, do

	mkdir build
	cd build
	cmake ..
	make

The build will create `serial-ram-emu.pio.h`, which the code expects to be in `pico/build/`.
