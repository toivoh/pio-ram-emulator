import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, FallingEdge, Timer, ClockCycles


IO_BITS = 2
RX_HEADER_CYCLES = 2
RX_DATA_CYCLES = 8

MSG_INDEX_BITS = 10

MAX_TX_BITS = 24

CFG_HEADER_BITS = 4
CFG_HEADER_SET_TX_INDEX = 0
CFG_HEADER_SET_TX_STOP_INDEX = 1
CFG_HEADER_SET_TX_COUNTDOWN = 2
CFG_HEADER_SET_INDEX = 3
CFG_HEADER_SET_DELAY = 4
CFG_HEADER_SET_COUNT = 5
CFG_HEADER_SET_PAYLOAD0 = 6
CFG_HEADER_SET_PAYLOAD1 = 7
CFG_HEADER_SET_RX_INDEX = 8
CFG_HEADER_READ_RX_INDEX = 9
CFG_HEADER_READ_RX_PAYLOAD = 10
CFG_HEADER_READ_RX_TIMESTAMP = 11


IO_MASK = 2**IO_BITS - 1
RX_DATA_BITS = IO_BITS * RX_DATA_CYCLES;
CFG_DATA_BITS = RX_DATA_BITS - CFG_HEADER_BITS


class Receiver:
	def __init__(self, IO_BITS=2, PAYLOAD_CYCLES=10):
		self.IO_BITS = IO_BITS
		self.PAYLOAD_CYCLES = PAYLOAD_CYCLES

		self.WORD_SIZE = self.PAYLOAD_CYCLES * self.IO_BITS
		self.tx_mask = (1 << self.IO_BITS) - 1

		self.rx_counter = 0
		self.rx_buffer = 0

		self.received = []

		#self.tx_buffer = 0
		#self.tx_delay_buffer = 0

		self.debug_print = False

	def step(self, rx):
		"""Takes rx, returns tx"""
		if self.rx_counter == 0:
			if (rx&1) == 0:
				self.rx_counter = 1
				self.rx_buffer = 0
				if self.debug_print: print("rx =", rx, "\tStart")
			else:
				if self.debug_print: print("rx =", rx, "\tWaiting")
				pass
		else:
			if self.debug_print: print("rx =", rx, "\trx_count =", self.rx_counter)

			self.rx_buffer = (self.rx_buffer | (rx << self.WORD_SIZE)) >> self.IO_BITS
			self.rx_counter += 1

			if self.rx_counter == self.PAYLOAD_CYCLES + 1:
				# Whole payload received
				self.rx_counter = 0
				self.received.append(self.rx_buffer >> 4)
				print("Received ", hex(self.rx_buffer >> 4))


#@cocotb.test()
async def test_run_mode(dut):
	dut._log.info("start")
	clock = Clock(dut.clk, 2, units="us")
	cocotb.start_soon(clock.start())

	tester = dut.tester

	# reset
	dut._log.info("reset")
	dut.rst_n.value = 0
	dut.rx_pins.value = IO_MASK
	dut.run_mode.value = 0
	await ClockCycles(dut.clk, 10)
	dut.rst_n.value = 1

	num_tx = 4
	timestamps, payloads = [], []
	timestamp = 1
	tx_count = 10
	for i in range(num_tx):
		delay = i
		tester.tx_delays[i].value = delay
		timestamps.append(timestamp)
		timestamp += delay + tx_count + 1

		tester.tx_counts[i].value = tx_count

		payload = 0x1234 + 0x1111*i
		tester.tx_payloads[i].value = (payload << 4) | 0xf
		payloads.append(payload)

	tester.tx_index.value = 0
	tester.tx_countdown.value = 0
	tester.tx_index_stop.value = num_tx

	tester.rx_index.value = 0

	await ClockCycles(dut.clk, 10)

	dut.run_mode.value = 1

	for i in range(60):
		dut.rx_pins.value = dut.tx_pins.value
		await ClockCycles(dut.clk, 1)


	rx_index = tester.rx_index.value.to_unsigned()
	print("rx_index =", rx_index)
	for i in range(num_tx):
		print(f"rx_payloads[{i}] = ", hex(tester.rx_payloads[i].value.to_unsigned()))
		print(f"rx_timestamps[{i}] = ", tester.rx_timestamps[i].value.to_unsigned())

	assert rx_index == num_tx

	for i in range(num_tx):
		assert(tester.rx_payloads[i].value.to_unsigned() == payloads[i])
		assert(tester.rx_timestamps[i].value.to_unsigned() == timestamps[i])

	dut.run_mode.value = 0
	await ClockCycles(dut.clk, 10)


@cocotb.test()
async def test_both_modes(dut):
	dut._log.info("start")
	clock = Clock(dut.clk, 2, units="us")
	cocotb.start_soon(clock.start())

	tester = dut.tester

	# reset
	dut._log.info("reset")
	dut.rst_n.value = 0
	dut.rx_pins.value = IO_MASK
	dut.run_mode.value = 0
	await ClockCycles(dut.clk, 10)
	dut.rst_n.value = 1


	cfg_payloads = []

	def add_cfg_command(header, data):
		cfg_payloads.append(((header | (data << CFG_HEADER_BITS))&(2**RX_DATA_BITS - 1)) << IO_BITS*(1 + RX_HEADER_CYCLES))


	# Set up commands to configure the tester before run mode
	# -------------------------------------------------------

	num_tx = 4
	timestamps, payloads = [], []
	timestamp = 1
	tx_count = 10

	for i in range(num_tx):
		add_cfg_command(CFG_HEADER_SET_INDEX, i)

		delay = i
		add_cfg_command(CFG_HEADER_SET_DELAY, delay)

		timestamps.append(timestamp)
		timestamp += delay + tx_count + 1

		add_cfg_command(CFG_HEADER_SET_COUNT, tx_count)

		payload = 0x1234 + 0x1111*i
		payloads.append(payload)
		payload = (payload << 4) | 0xf
		#tester.tx_payloads[i].value = (payload << 4) | 0xf
		add_cfg_command(CFG_HEADER_SET_PAYLOAD0, payload)
		add_cfg_command(CFG_HEADER_SET_PAYLOAD1, payload >> CFG_DATA_BITS)

	add_cfg_command(CFG_HEADER_SET_TX_INDEX, 0)
	add_cfg_command(CFG_HEADER_SET_TX_STOP_INDEX, num_tx)
	add_cfg_command(CFG_HEADER_SET_TX_COUNTDOWN, 0)
	add_cfg_command(CFG_HEADER_SET_RX_INDEX, 0)


	print("setup payloads =", cfg_payloads)


	# Send the commands
	# -----------------

	for payload in cfg_payloads:
		for i in range(8+3):
			dut.rx_pins.value = payload & IO_MASK
			payload >>= IO_BITS

			await ClockCycles(dut.clk, 1)

	dut.rx_pins.value = IO_MASK
	await ClockCycles(dut.clk, 10)


	# Enable run mode, connect TX pins to RX pins
	# -------------------------------------------

	dut.run_mode.value = 1

	for i in range(60):
		dut.rx_pins.value = dut.tx_pins.value
		await ClockCycles(dut.clk, 1)

	# Test that the right data was captured
	# -------------------------------------

	rx_index = tester.rx_index.value.to_unsigned()
	print("rx_index =", rx_index)
	for i in range(num_tx):
		print(f"rx_payloads[{i}] = ", hex(tester.rx_payloads[i].value.to_unsigned()))
		print(f"rx_timestamps[{i}] = ", tester.rx_timestamps[i].value.to_unsigned())

	assert rx_index == num_tx

	for i in range(num_tx):
		assert(tester.rx_payloads[i].value.to_unsigned() == payloads[i])
		assert(tester.rx_timestamps[i].value.to_unsigned() == timestamps[i])

	# Back to cfg mode
	# ----------------

	dut.run_mode.value = 0
	await ClockCycles(dut.clk, 10)


	# Set up commands to read ut the captured data
	# --------------------------------------------

	cfg_payloads = []

	add_cfg_command(CFG_HEADER_READ_RX_INDEX, 0)
	for i in range(num_tx):
		add_cfg_command(CFG_HEADER_READ_RX_PAYLOAD, i)
		add_cfg_command(CFG_HEADER_READ_RX_TIMESTAMP, i)

	print("readout payloads =", cfg_payloads)


	# Send the commands and receive the data
	# --------------------------------------

	receiver = Receiver()

	for payload in cfg_payloads:
		for i in range(8+3):
			dut.rx_pins.value = payload & IO_MASK
			payload >>= IO_BITS

			receiver.step(dut.tx_pins.value.to_unsigned())
			await ClockCycles(dut.clk, 1)

		dut.rx_pins.value = IO_MASK # Add a stop bit
		receiver.step(dut.tx_pins.value.to_unsigned())
		await ClockCycles(dut.clk, 1)

	dut.rx_pins.value = IO_MASK
	for i in range(20):
		receiver.step(dut.tx_pins.value.to_unsigned())
		await ClockCycles(dut.clk, 1)

	received = receiver.received
	print("received: ", received)

	index = 0
	assert received[index] == num_tx
	index += 1

	for i in range(num_tx):
		assert received[index] == payloads[i]
		index += 1
		assert received[index] == timestamps[i]
		index += 1
