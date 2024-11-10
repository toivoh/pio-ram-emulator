#include "hardware/structs/bus_ctrl.h"
#include "hardware/dma.h"

#include "ram-emu.h"

#include "build/serial-ram-emu.pio.h"

uint16_t __attribute__((section(".spi_ram.emu_ram"))) emu_ram[65536];


void ram_emu_set_16bit_mode(PioRamEmulator *emu) {
	emu->wdata_is_16bit = true;
	emu->rdata_is_16bit = true;
}

void ram_emu_set_8bit_mode(PioRamEmulator *emu) {
	emu->wdata_is_16bit = false;
	emu->rdata_is_16bit = false;
}

void ram_emu_init_settings(PioRamEmulator *emu) {
	ram_emu_set_16bit_mode(emu);
}


bool add_psm(PSM *psm, PIO pio, const pio_program_t *program) {
	if (!pio_can_add_program(pio, program)) return false;
	psm->pio = pio;
	psm->offset = pio_add_program(pio, program);
	int sm = pio_claim_unused_sm(pio, false);
	if (sm == -1) return false;
	psm->sm = sm;
	return true;
}

bool clone_psm(PSM *psm, const PSM *source) {
	psm->pio = source->pio;
	psm->offset = source->offset;
	int sm = pio_claim_unused_sm(psm->pio, false);
	if (sm == -1) return false;
	psm->sm = sm;
	return true;
}

void init_dma(PioRamEmulator *emu) {
	// Prioritize DMA over CPU cores
	// ----------------------------
	hw_clear_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC0_BITS | BUSCTRL_BUS_PRIORITY_PROC1_BITS);
	hw_set_bits(  &bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_DMA_R_BITS | BUSCTRL_BUS_PRIORITY_DMA_W_BITS);

	// Allocate DMA channels
	// ---------------------
	emu->rx_wdata_channel = dma_claim_unused_channel(true);
	emu->rx_waddr_channel = dma_claim_unused_channel(true);
	emu->rx_wcount_channel = dma_claim_unused_channel(true);

	emu->tx_rdata_channel = dma_claim_unused_channel(true);
	emu->rx_raddr_channel = dma_claim_unused_channel(true);
	emu->rx_rcount_channel = dma_claim_unused_channel(true);
}

void ram_emu_configure_dma(PioRamEmulator *emu, bool enable) {
	// Writing
	// =======

	// RX wdata channel
	// ----------------
	volatile uint32_t *rx_wdata_channel_src  = (volatile uint32_t *)&(emu->rx_wdata_psm.pio->rxf[emu->rx_wdata_psm.sm]);
	volatile uint32_t *rx_wdata_channel_dest = (volatile uint32_t *)emu_ram;

	dma_channel_config rx_wdata_cfg = dma_channel_get_default_config(emu->rx_wdata_channel);

	channel_config_set_read_increment(&rx_wdata_cfg, false);
	channel_config_set_write_increment(&rx_wdata_cfg, true);
	if (enable) channel_config_set_dreq(&rx_wdata_cfg, pio_get_dreq(emu->rx_wdata_psm.pio, emu->rx_wdata_psm.sm, false)); // dreq from RX FIFO
	channel_config_set_transfer_data_size(&rx_wdata_cfg, emu->wdata_is_16bit ? DMA_SIZE_16 : DMA_SIZE_8);

	//dma_channel_configure(emu->rx_wdata_channel, &rx_wdata_cfg, rx_wdata_channel_dest, rx_wdata_channel_src, sizeof(emu_ram)/2, true); // Start the channel, very big transfer count
	dma_channel_configure(emu->rx_wdata_channel, &rx_wdata_cfg, rx_wdata_channel_dest, rx_wdata_channel_src, 1, false); // trans_count = 1, don't start

	// RX waddr channel
	// ----------------
	volatile uint32_t *rx_waddr_channel_src  = (volatile uint32_t *)&(emu->rx_waddr_psm.pio->rxf[emu->rx_waddr_psm.sm]);
	volatile uint32_t *rx_waddr_channel_dest = &(dma_channel_hw_addr(emu->rx_wdata_channel)->al2_write_addr_trig);

	dma_channel_config rx_waddr_cfg = dma_channel_get_default_config(emu->rx_waddr_channel);

	channel_config_set_read_increment(&rx_waddr_cfg, false);
	if (enable) channel_config_set_dreq(&rx_waddr_cfg, pio_get_dreq(emu->rx_waddr_psm.pio, emu->rx_waddr_psm.sm, false)); // dreq from RX FIFO

	// Start the channel, very big transfer count
	dma_channel_configure(emu->rx_waddr_channel, &rx_waddr_cfg, rx_waddr_channel_dest, rx_waddr_channel_src, -1, enable);

	// RX wcount channel
	// -----------------
	volatile uint32_t *rx_wcount_channel_src  = (volatile uint32_t *)&(emu->rx_wcount_psm.pio->rxf[emu->rx_wcount_psm.sm]);
	volatile uint32_t *rx_wcount_channel_dest = &(dma_channel_hw_addr(emu->rx_wdata_channel)->transfer_count);

	dma_channel_config rx_wcount_cfg = dma_channel_get_default_config(emu->rx_wcount_channel);

	channel_config_set_read_increment(&rx_wcount_cfg, false);
	if (enable) channel_config_set_dreq(&rx_wcount_cfg, pio_get_dreq(emu->rx_wcount_psm.pio, emu->rx_wcount_psm.sm, false)); // dreq from RX FIFO

	// Start the channel, very big transfer count
	dma_channel_configure(emu->rx_wcount_channel, &rx_wcount_cfg, rx_wcount_channel_dest, rx_wcount_channel_src, -1, enable);

	// Reading
	// =======

	// TX rdata channel
	// ----------------
	volatile uint32_t *tx_rdata_channel_dest = (volatile uint32_t *)&(emu->tx_rdata_psm.pio->txf[emu->tx_rdata_psm.sm]);
	volatile uint32_t *tx_rdata_channel_src  = (volatile uint32_t *)emu_ram;

	dma_channel_config tx_rdata_cfg = dma_channel_get_default_config(emu->tx_rdata_channel);

	channel_config_set_read_increment(&tx_rdata_cfg, true);
	channel_config_set_write_increment(&tx_rdata_cfg, false);
	if (enable) channel_config_set_dreq(&tx_rdata_cfg, pio_get_dreq(emu->tx_rdata_psm.pio, emu->tx_rdata_psm.sm, true)); // dreq from TX FIFO
	channel_config_set_transfer_data_size(&tx_rdata_cfg, emu->rdata_is_16bit ? DMA_SIZE_16 : DMA_SIZE_8);

	//dma_channel_configure(emu->tx_rdata_channel, &tx_rdata_cfg, tx_rdata_channel_dest, tx_rdata_channel_src, sizeof(emu_ram)/2, true); // Start the channel, very big transfer count
	dma_channel_configure(emu->tx_rdata_channel, &tx_rdata_cfg, tx_rdata_channel_dest, tx_rdata_channel_src, 1, false); // trans_count = 1, don't start

	// RX raddr channel
	// ----------------
	volatile uint32_t *rx_raddr_channel_src  = (volatile uint32_t *)&(emu->rx_raddr_psm.pio->rxf[emu->rx_raddr_psm.sm]);
	volatile uint32_t *rx_raddr_channel_dest = &(dma_channel_hw_addr(emu->tx_rdata_channel)->al3_read_addr_trig);

	dma_channel_config rx_raddr_cfg = dma_channel_get_default_config(emu->rx_raddr_channel);

	channel_config_set_read_increment(&rx_raddr_cfg, false);
	if (enable) channel_config_set_dreq(&rx_raddr_cfg, pio_get_dreq(emu->rx_raddr_psm.pio, emu->rx_raddr_psm.sm, false)); // dreq from RX FIFO

	// Start the channel, very big transfer count
	dma_channel_configure(emu->rx_raddr_channel, &rx_raddr_cfg, rx_raddr_channel_dest, rx_raddr_channel_src, -1, enable);

	// RX rcount channel
	// -----------------
	volatile uint32_t *rx_rcount_channel_src  = (volatile uint32_t *)&(emu->rx_rcount_psm.pio->rxf[emu->rx_rcount_psm.sm]);
	volatile uint32_t *rx_rcount_channel_dest = &(dma_channel_hw_addr(emu->tx_rdata_channel)->transfer_count);

	dma_channel_config rx_rcount_cfg = dma_channel_get_default_config(emu->rx_rcount_channel);

	channel_config_set_read_increment(&rx_rcount_cfg, false);
	if (enable) channel_config_set_dreq(&rx_rcount_cfg, pio_get_dreq(emu->rx_rcount_psm.pio, emu->rx_rcount_psm.sm, false)); // dreq from RX FIFO

	// Start the channel, very big transfer count
	dma_channel_configure(emu->rx_rcount_channel, &rx_rcount_cfg, rx_rcount_channel_dest, rx_rcount_channel_src, -1, enable);
}

void ram_emu_stop_dma(PioRamEmulator *emu) {
	dma_channel_abort(emu->rx_wdata_channel);
	dma_channel_abort(emu->rx_waddr_channel);
	dma_channel_abort(emu->rx_wcount_channel);

	dma_channel_abort(emu->tx_rdata_channel);
	dma_channel_abort(emu->rx_raddr_channel);
	dma_channel_abort(emu->rx_rcount_channel);
}


bool ram_emu_init(PioRamEmulator *emu, int rx_pin_base, int tx_pin_base, bool start_dma) {
	// Start PIO
	// =========
	PIO pio = pio0;
	PSM *psm;
	bool ok = true;

	// TX rdata
	// --------
	psm = &(emu->tx_rdata_psm);
	if (add_psm(psm, pio, &sbio2_tx_program)) sbio2_tx_program_init(pio, psm->sm, psm->offset, tx_pin_base); else ok = false;

	// RX wdata
	// --------
	psm = &(emu->rx_wdata_psm);
	if (add_psm(psm, pio, &sbio2_rx_10_program)) sbio2_rx_10_program_init(pio, psm->sm, psm->offset, rx_pin_base); else ok = false;

	// RX wcount
	// ---------
	psm = &(emu->rx_wcount_psm);
	if (add_psm(psm, pio, &sbio2_rx_00_program)) sbio2_rx_00_program_init(pio, psm->sm, psm->offset, rx_pin_base, rx_pin_base); else ok = false;

	// RX rcount-- initialize after RX wcount (clone)
	// ----------------------------------------------
	psm = &(emu->rx_rcount_psm);
	if (clone_psm(psm, &(emu->rx_wcount_psm))) sbio2_rx_00_program_init(pio, psm->sm, psm->offset, rx_pin_base, rx_pin_base + 1); else ok = false;

	pio = pio1;

	// RX waddr
	// --------
	psm = &(emu->rx_waddr_psm);
	if (emu->wdata_is_16bit) {
		if (add_psm(psm, pio, &sbio2_rx_addr_01_program)) sbio2_rx_addr_01_program_init(pio, psm->sm, psm->offset, rx_pin_base, rx_pin_base); else ok = false;
	} else {
		if (add_psm(psm, pio, &sbio2_rx_byte_addr_01_program)) sbio2_rx_byte_addr_01_program_init(pio, psm->sm, psm->offset, rx_pin_base, rx_pin_base); else ok = false;
	}
	pio_sm_put(emu->rx_waddr_psm.pio, emu->rx_waddr_psm.sm, ((int)emu_ram)>>(16+emu->wdata_is_16bit)); // Initialize aligned buffer address

	// RX raddr -- initialize after RX waddr (clone if possible)
	// ---------------------------------------------------------
	// The only difference compared to the RX waddr program is the jump pin (and possibly the address granularity)
	psm = &(emu->rx_raddr_psm);
	if (emu->rdata_is_16bit == emu->wdata_is_16bit) {
		// Clone RX waddr program
		if (emu->wdata_is_16bit) {
			if (clone_psm(psm, &(emu->rx_waddr_psm))) sbio2_rx_addr_01_program_init(pio, psm->sm, psm->offset, rx_pin_base, rx_pin_base + 1); else ok = false;
		} else {
			if (clone_psm(psm, &(emu->rx_waddr_psm))) sbio2_rx_byte_addr_01_program_init(pio, psm->sm, psm->offset, rx_pin_base, rx_pin_base + 1); else ok = false;
		}
	} else {
		// Can't clone
		if (emu->wdata_is_16bit) {
			if (add_psm(psm, pio, &sbio2_rx_addr_01_program)) sbio2_rx_addr_01_program_init(pio, psm->sm, psm->offset, rx_pin_base, rx_pin_base + 1); else ok = false;
		} else {
			if (add_psm(psm, pio, &sbio2_rx_byte_addr_01_program)) sbio2_rx_byte_addr_01_program_init(pio, psm->sm, psm->offset, rx_pin_base, rx_pin_base + 1); else ok = false;
		}
	}
	pio_sm_put(emu->rx_raddr_psm.pio, emu->rx_raddr_psm.sm, ((int)emu_ram)>>(16+emu->rdata_is_16bit)); // Initialize aligned buffer address

	// Set up DMA
	// ==========
	init_dma(emu);
	if (ok && start_dma) ram_emu_configure_dma(emu, true);

	return ok;
}
