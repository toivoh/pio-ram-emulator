#pragma once

#include "hardware/pio.h"


typedef struct {
	PIO pio;
	uint sm;
	uint offset;
} PSM;


extern uint16_t emu_ram[65536];
static const int emu_ram_elements = 65536;

extern PSM tx_rdata_psm;
extern PSM rx_wdata_psm, rx_waddr_psm, rx_wcount_psm;
extern PSM               rx_raddr_psm, rx_rcount_psm;


bool ram_emu_init(int rx_pin_base, int tx_pin_base, bool start_dma);
void ram_emu_configure_dma(bool enable);
void ram_emu_stop_dma();


bool add_psm(PSM *psm, PIO pio, const pio_program_t *program);
bool clone_psm(PSM *psm, const PSM *source);
