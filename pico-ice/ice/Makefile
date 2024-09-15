YOSYS = $(OSS_CAD_SUITE)/bin/yosys
NEXTPNR = $(OSS_CAD_SUITE)/bin/nextpnr-ice40
ICEPACK = $(OSS_CAD_SUITE)/bin/icepack
DFU_UTIL = $(OSS_CAD_SUITE)/bin/dfu-util
BIN2UF2 = bin2uf2

#RTL = serial-ram-emu-test.sv # Doesn't work for capture test, use FPGA code from ram-test1 for correct sync
#RTL = fb-test.sv
#RTL = tilemap-test.sv
#PCF = serial-ram-emu-ice40.pcf

#RTL = serial-ram-emu-rw22-test.sv
#PCF = serial-ram-emu-rw22-ice40.pcf

RTL = sbio2_tester2.sv sb_io.v
PCF = sbio2_tester.pcf

all: ice40.bin ice40.uf2

clean:
	$(RM) *.json *.asc *.bin *.uf2

prog: ice40.bin
	$(DFU_UTIL) -d 1209:b1c0 -a 1 -D ice40.bin -R

ice40.bin: $(RTL) $(PCF) Makefile
	$(YOSYS) -q -p "read_verilog -sv $(RTL); synth_ice40 -top top -json $*.json"
	$(NEXTPNR) -q --randomize-seed --up5k --package sg48 --pcf $(PCF) --json $*.json --asc $*.asc -l nextpnr.log
	$(ICEPACK) $*.asc $@

.bin.uf2:
	$(BIN2UF2) -o $@ $<

.SUFFIXES: .v .sv .asc .bin .uf2
