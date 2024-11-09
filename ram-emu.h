#pragma once

#include "hardware/pio.h"


typedef struct {
	PIO pio;
	uint sm;
	uint offset;
} PSM;

typedef struct {
	PSM tx_rdata_psm;
	PSM rx_wdata_psm, rx_waddr_psm, rx_wcount_psm;
	PSM               rx_raddr_psm, rx_rcount_psm;

	int rx_wdata_channel, rx_waddr_channel, rx_wcount_channel;
	int tx_rdata_channel, rx_raddr_channel, rx_rcount_channel;
} PioRamEmulator;


extern uint16_t emu_ram[65536];
static const int emu_ram_elements = 65536;


bool ram_emu_init(PioRamEmulator *emu, int rx_pin_base, int tx_pin_base, bool start_dma);
void ram_emu_configure_dma(PioRamEmulator *emu, bool enable);
void ram_emu_stop_dma(PioRamEmulator *emu);


bool add_psm(PSM *psm, PIO pio, const pio_program_t *program);
bool clone_psm(PSM *psm, const PSM *source);
