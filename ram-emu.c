#include "hardware/structs/bus_ctrl.h"
#include "hardware/dma.h"

#include "ram-emu.h"

#include "build/serial-ram-emu.pio.h"

uint16_t __attribute__((section(".spi_ram.emu_ram"))) emu_ram[65536];

PSM tx_rdata_psm;
PSM rx_wdata_psm, rx_waddr_psm, rx_wcount_psm;
PSM               rx_raddr_psm, rx_rcount_psm;

int rx_wdata_channel, rx_waddr_channel, rx_wcount_channel;
int tx_rdata_channel, rx_raddr_channel, rx_rcount_channel;


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

void init_dma() {
	// Prioritize DMA over CPU cores
	// ----------------------------
	hw_clear_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC0_BITS | BUSCTRL_BUS_PRIORITY_PROC1_BITS);
	hw_set_bits(  &bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_DMA_R_BITS | BUSCTRL_BUS_PRIORITY_DMA_W_BITS);

	// Allocate DMA channels
	// ---------------------
	rx_wdata_channel = dma_claim_unused_channel(true);
	rx_waddr_channel = dma_claim_unused_channel(true);
	rx_wcount_channel = dma_claim_unused_channel(true);

	tx_rdata_channel = dma_claim_unused_channel(true);
	rx_raddr_channel = dma_claim_unused_channel(true);
	rx_rcount_channel = dma_claim_unused_channel(true);
}

void ram_emu_configure_dma(bool enable) {
	// Writing
	// =======

	// RX wdata channel
	// ----------------
	volatile uint32_t *rx_wdata_channel_src  = (volatile uint32_t *)&(rx_wdata_psm.pio->rxf[rx_wdata_psm.sm]);
	volatile uint32_t *rx_wdata_channel_dest = (volatile uint32_t *)emu_ram;

	dma_channel_config rx_wdata_cfg = dma_channel_get_default_config(rx_wdata_channel);

	channel_config_set_read_increment(&rx_wdata_cfg, false);
	channel_config_set_write_increment(&rx_wdata_cfg, true);
	if (enable) channel_config_set_dreq(&rx_wdata_cfg, pio_get_dreq(rx_wdata_psm.pio, rx_wdata_psm.sm, false)); // dreq from RX FIFO
	channel_config_set_transfer_data_size(&rx_wdata_cfg, DMA_SIZE_16);

	//dma_channel_configure(rx_wdata_channel, &rx_wdata_cfg, rx_wdata_channel_dest, rx_wdata_channel_src, sizeof(emu_ram)/2, true); // Start the channel, very big transfer count
	dma_channel_configure(rx_wdata_channel, &rx_wdata_cfg, rx_wdata_channel_dest, rx_wdata_channel_src, 1, false); // trans_count = 1, don't start

	// RX waddr channel
	// ----------------
	volatile uint32_t *rx_waddr_channel_src  = (volatile uint32_t *)&(rx_waddr_psm.pio->rxf[rx_waddr_psm.sm]);
	volatile uint32_t *rx_waddr_channel_dest = &(dma_channel_hw_addr(rx_wdata_channel)->al2_write_addr_trig);

	dma_channel_config rx_waddr_cfg = dma_channel_get_default_config(rx_waddr_channel);

	channel_config_set_read_increment(&rx_waddr_cfg, false);
	if (enable) channel_config_set_dreq(&rx_waddr_cfg, pio_get_dreq(rx_waddr_psm.pio, rx_waddr_psm.sm, false)); // dreq from RX FIFO

	// Start the channel, very big transfer count
	dma_channel_configure(rx_waddr_channel, &rx_waddr_cfg, rx_waddr_channel_dest, rx_waddr_channel_src, -1, enable);

	// RX wcount channel
	// -----------------
	volatile uint32_t *rx_wcount_channel_src  = (volatile uint32_t *)&(rx_wcount_psm.pio->rxf[rx_wcount_psm.sm]);
	volatile uint32_t *rx_wcount_channel_dest = &(dma_channel_hw_addr(rx_wdata_channel)->transfer_count);

	dma_channel_config rx_wcount_cfg = dma_channel_get_default_config(rx_wcount_channel);

	channel_config_set_read_increment(&rx_wcount_cfg, false);
	if (enable) channel_config_set_dreq(&rx_wcount_cfg, pio_get_dreq(rx_wcount_psm.pio, rx_wcount_psm.sm, false)); // dreq from RX FIFO

	// Start the channel, very big transfer count
	dma_channel_configure(rx_wcount_channel, &rx_wcount_cfg, rx_wcount_channel_dest, rx_wcount_channel_src, -1, enable);

	// Reading
	// =======

	// TX rdata channel
	// ----------------
	volatile uint32_t *tx_rdata_channel_dest = (volatile uint32_t *)&(tx_rdata_psm.pio->txf[tx_rdata_psm.sm]);
	volatile uint32_t *tx_rdata_channel_src  = (volatile uint32_t *)emu_ram;

	dma_channel_config tx_rdata_cfg = dma_channel_get_default_config(tx_rdata_channel);

	channel_config_set_read_increment(&tx_rdata_cfg, true);
	channel_config_set_write_increment(&tx_rdata_cfg, false);
	if (enable) channel_config_set_dreq(&tx_rdata_cfg, pio_get_dreq(tx_rdata_psm.pio, tx_rdata_psm.sm, true)); // dreq from TX FIFO
	channel_config_set_transfer_data_size(&tx_rdata_cfg, DMA_SIZE_16);

	//dma_channel_configure(tx_rdata_channel, &tx_rdata_cfg, tx_rdata_channel_dest, tx_rdata_channel_src, sizeof(emu_ram)/2, true); // Start the channel, very big transfer count
	dma_channel_configure(tx_rdata_channel, &tx_rdata_cfg, tx_rdata_channel_dest, tx_rdata_channel_src, 1, false); // trans_count = 1, don't start

	// RX raddr channel
	// ----------------
	volatile uint32_t *rx_raddr_channel_src  = (volatile uint32_t *)&(rx_raddr_psm.pio->rxf[rx_raddr_psm.sm]);
	volatile uint32_t *rx_raddr_channel_dest = &(dma_channel_hw_addr(tx_rdata_channel)->al3_read_addr_trig);

	dma_channel_config rx_raddr_cfg = dma_channel_get_default_config(rx_raddr_channel);

	channel_config_set_read_increment(&rx_raddr_cfg, false);
	if (enable) channel_config_set_dreq(&rx_raddr_cfg, pio_get_dreq(rx_raddr_psm.pio, rx_raddr_psm.sm, false)); // dreq from RX FIFO

	// Start the channel, very big transfer count
	dma_channel_configure(rx_raddr_channel, &rx_raddr_cfg, rx_raddr_channel_dest, rx_raddr_channel_src, -1, enable);

	// RX rcount channel
	// -----------------
	volatile uint32_t *rx_rcount_channel_src  = (volatile uint32_t *)&(rx_rcount_psm.pio->rxf[rx_rcount_psm.sm]);
	volatile uint32_t *rx_rcount_channel_dest = &(dma_channel_hw_addr(tx_rdata_channel)->transfer_count);

	dma_channel_config rx_rcount_cfg = dma_channel_get_default_config(rx_rcount_channel);

	channel_config_set_read_increment(&rx_rcount_cfg, false);
	if (enable) channel_config_set_dreq(&rx_rcount_cfg, pio_get_dreq(rx_rcount_psm.pio, rx_rcount_psm.sm, false)); // dreq from RX FIFO

	// Start the channel, very big transfer count
	dma_channel_configure(rx_rcount_channel, &rx_rcount_cfg, rx_rcount_channel_dest, rx_rcount_channel_src, -1, enable);
}

void ram_emu_stop_dma() {
	dma_channel_abort(rx_wdata_channel);
	dma_channel_abort(rx_waddr_channel);
	dma_channel_abort(rx_wcount_channel);

	dma_channel_abort(tx_rdata_channel);
	dma_channel_abort(rx_raddr_channel);
	dma_channel_abort(rx_rcount_channel);
}


bool ram_emu_init(int rx_pin_base, int tx_pin_base, bool start_dma) {
	// Start PIO
	// =========
	PIO pio = pio0;
	PSM *psm;
	bool ok = true;

	// TX rdata
	// --------
	psm = &tx_rdata_psm;
	if (add_psm(psm, pio, &sbio2_tx_program)) sbio2_tx_program_init(pio, psm->sm, psm->offset, tx_pin_base); else ok = false;

	// RX wdata
	// --------
	psm = &rx_wdata_psm;
	if (add_psm(psm, pio, &sbio2_rx_10_program)) sbio2_rx_10_program_init(pio, psm->sm, psm->offset, rx_pin_base); else ok = false;

	// RX wcount
	// ---------
	psm = &rx_wcount_psm;
	if (add_psm(psm, pio, &sbio2_rx_00_program)) sbio2_rx_00_program_init(pio, psm->sm, psm->offset, rx_pin_base, rx_pin_base); else ok = false;

	// RX rcount-- initialize after RX waddr
	// -------------------------------------
	psm = &rx_rcount_psm;
	if (clone_psm(psm, &rx_wcount_psm)) sbio2_rx_00_program_init(pio, psm->sm, psm->offset, rx_pin_base, rx_pin_base + 1); else ok = false;

	pio = pio1;

	// RX waddr
	// --------
	psm = &rx_waddr_psm;
	if (add_psm(psm, pio, &sbio2_rx_addr_01_program)) sbio2_rx_addr_01_program_init(pio, psm->sm, psm->offset, rx_pin_base, rx_pin_base); else ok = false;
	pio_sm_put(rx_waddr_psm.pio, rx_waddr_psm.sm, ((int)emu_ram)>>17); // Initialize aligned buffer address

	// RX raddr -- initialize after RX waddr
	// -------------------------------------
	psm = &rx_raddr_psm;
	if (clone_psm(psm, &rx_waddr_psm)) sbio2_rx_addr_01_program_init(pio, psm->sm, psm->offset, rx_pin_base, rx_pin_base + 1); else ok = false;
	pio_sm_put(rx_raddr_psm.pio, rx_raddr_psm.sm, ((int)emu_ram)>>17); // Initialize aligned buffer address

	// Set up DMA
	// ==========
	init_dma();
	if (start_dma) ram_emu_configure_dma(true);

	return ok;
}
