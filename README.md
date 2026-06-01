# Industrial Safety Monitoring CAN Network

A two-node CAN bus safety system built on STM32, demonstrating embedded safety patterns used in automotive and industrial control: message integrity (CRC, counters), heartbeat/watchdog monitoring, and deterministic safe-state transitions. Python-based CAN traffic logger included for analysis.

**Project status:** 🚧 In progress — Week 1 of 2

## Goals

- Build a working two-node CAN safety network on real hardware
- Demonstrate AUTOSAR-flavoured patterns (DEM-like diagnostics, COM-like validation, WdgM-like watchdog) in plain HAL code
- Produce quantitative timing analysis with a Python logger
- Document the build day-by-day as a learning record

## Hardware

- 2× STM32 Nucleo-F446RE
- 2× SN65HVD230 CAN transceivers
- 1× BME280 temperature/humidity sensor (I2C)
- 1× SG90 servo motor
- 1× Innomaker USB-CAN adapter for PC-side logging
- 120 Ω termination resistors, breadboard, jumper wires

## Repository structure
Industrial Safety Monitoring CAN Network/
├── blink_test/         # Day 1–2: toolchain validation, LED blink, UART printf
├── images/             # screenshots and hardware photos, organised by day
└── ...                 # (more sub-projects added as work progresses)

## Build log

### Week 1 — STM32 basics, I2C sensor, UART debugging

#### Day 1 (2026-05-13) — Toolchain setup and LED blink

Validated the STM32CubeMX → STM32CubeIDE workflow on a Nucleo-F446RE. Used Board Selector to generate a baseline project — this pre-configures the system clock, PA5 (user LED LD2), PC13 (user button B1), and routes USART2 (PA2/PA3) to the ST-Link virtual COM port. Configured the code generator to emit one `.c/.h` file per peripheral (cleaner than the default single-file dump) and to preserve user code between `/* USER CODE BEGIN ... */` markers on regeneration.

Implemented LED blink three ways to internalise the abstraction layers:
- `HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin)` — HAL wrapper, function call
- `GPIOA->ODR ^= GPIO_ODR_OD5` — CMSIS read-modify-write
- `GPIOA->BSRR = GPIO_BSRR_BS5` — CMSIS atomic single-cycle write (preferred for ISR safety)

Covered why the BSRR pattern matters once interrupts share a GPIO port with the main loop — a foundation for the multi-LED status indicators planned for Node A.

<p align="center">
  <img src="images/day01-blink-toolchain/04-led-blink-hardware.jpeg" width="500">
  <br>
  <em>First firmware running on hardware — green LD2 blinking at 1 Hz on the Nucleo-F446RE.</em>
</p>

<details>
<summary>Setup screenshots</summary>

<p align="center">
  <img src="images/day01-blink-toolchain/01-cubemx-board-selector-pinout.png" width="700">
  <br>
  <em>CubeMX Board Selector pinout view — PA5 (LD2), PA2/PA3 (USART2), PC13 (B1) pre-configured.</em>
</p>

<p align="center">
  <img src="images/day01-blink-toolchain/02-cubemx-code-generator-settings.png" width="700">
  <br>
  <em>Code Generator settings — per-peripheral file split and user-code preservation enabled.</em>
</p>

<p align="center">
  <img src="images/day01-blink-toolchain/03-cubeide-project-structure.png" width="700">
  <br>
  <em>Generated project structure in STM32CubeIDE.</em>
</p>

</details>

#### Day 2 (2026-05-14) — UART printf debugging via ST-Link virtual COM port

Retargeted stdout to USART2 (115200 8N1) by overriding the `_write()` syscall to forward bytes through `HAL_UART_Transmit`. The Nucleo's onboard ST-Link MCU also acts as a USB-to-serial bridge, so no extra hardware is required for the PC to see the output. Boot banner includes `__DATE__`/`__TIME__` compiler macros — a sanity check that the firmware currently running matches the most recent build.

Implemented an in-line `printf` profiler using `HAL_GetTick()` before and after each print call.

**Measurements:**
- Boot-to-first-output: ~5 ms (clock-tree stabilisation + boot banner transmission)
- Each printf at 115200 baud: ~2–3 ms, dominated by serial transmit time (each character ≈87 µs on the wire; `HAL_UART_Transmit` blocks until the last byte has left)
- Nominal 500 ms `HAL_Delay` loop drifts to 503–505 ms once printing is added — the observer effect on timing

This is the first quantitative observation of how blocking I/O distorts deterministic scheduling — directly motivates the FreeRTOS-vs-bare-metal comparison planned for Project 3.

<p align="center">
  <img src="images/day02-uart-printf/03-putty-printf-profiling.png" width="700">
  <br>
  <em>Live UART output in PuTTY showing tick counter, system uptime, and self-measured printf duration.</em>
</p>

<details>
<summary>Setup and earlier iteration</summary>

<p align="center">
  <img src="images/day02-uart-printf/01-cubemx-usart2-config.png" width="700">
  <br>
  <em>USART2 configuration in CubeMX: 115200 8N1, hardware flow control disabled.</em>
</p>

<p align="center">
  <img src="images/day02-uart-printf/02-putty-tick-counter.png" width="700">
  <br>
  <em>First working UART output — boot banner plus tick counter (before adding uptime/profiling).</em>
</p>

</details>

#### Day 3 (2026-05-15) — I2C bus and BME280 sensor discovery

Enabled I2C1 peripheral on PB8 (SCL) and PB9 (SDA) at 100 kHz Standard Mode via CubeMX. Validated the regeneration workflow: reopened the `.ioc`, enabled a new peripheral, regenerated code, and confirmed that previously-written user code inside `/* USER CODE BEGIN ... */` markers survived intact — this is the workflow that scales as the project grows.

Wired a BME280 temperature/humidity/pressure sensor breakout to the Nucleo's Arduino-compatible header (VIN → 3V3, GND → GND, SCL → D15/PB8, SDA → D14/PB9). Implemented two operations at boot:

1. **I2C bus scanner** — iterates all 127 possible 7-bit addresses, calling `HAL_I2C_IsDeviceReady` on each. Any address that ACKs gets printed.
2. **Chip ID verification** — once a device is found, reads register `0xD0` via `HAL_I2C_Mem_Read`. The BME280 datasheet guarantees this register returns `0x60`. Matching value confirms not just I2C connectivity, but that the correct sensor type is on the bus (vs. e.g. the older BMP280 which returns `0x58`).

Covered the HAL address-shifting convention (`<< 1` to make space for the read/write bit) — the most common single source of "I2C silently doesn't work" bugs in STM32 code.

<p align="center">
  <img src="images/day03-i2c-bme280/02-bme280-wiring.jpeg" width="500">
  <br>
  <em>BME280 sensor wired to the Nucleo over I2C — VIN, GND, SCL, SDA on the Arduino-compatible header.</em>
</p>

<p align="center">
  <img src="images/day03-i2c-bme280/03-putty-bme280-detected.png" width="700">
  <br>
  <em>Boot output: I2C scanner finds the sensor at 0x76; chip ID read at register 0xD0 returns 0x60, confirming the device is a genuine BME280.</em>
</p>

<details>
<summary>CubeMX I2C configuration</summary>

<p align="center">
  <img src="images/day03-i2c-bme280/01-cubemx-i2c1-config.png" width="700">
  <br>
  <em>I2C1 peripheral configured: Standard Mode, 100 kHz, 7-bit addressing. PB8/PB9 auto-routed as SCL/SDA.</em>
</p>

</details>

#### Day 4 (2026-05-17) — Live temperature, humidity, and pressure readings

Extended the BME280 work from chip-ID discovery to full sensor operation. Created dedicated driver files (`bme280.h` / `bme280.c`) to keep `main.c` focused on application logic — the structural pattern used in production embedded code.

Implementation steps:
1. **Read 26 bytes of calibration data** from registers `0x88..0xA1` and another 7 bytes from `0xE1..0xE7` at init. Each BME280 chip is factory-calibrated; these coefficients are unique per device and must be read from the chip's non-volatile memory.
2. **Configure measurement mode** — wrote `0xF2 = 0x01` (humidity oversampling ×1), `0xF4 = 0x27` (temp ×1, pressure ×1, normal continuous mode), `0xF5 = 0xA0` (1000 ms standby, filter off).
3. **Read 8 bytes of raw measurement** from `0xF7..0xFE` every second — 20-bit raw values for pressure, temperature, and humidity in a single I2C transaction.
4. **Apply Bosch's compensation algorithm** (datasheet section 4.2.3) — converts raw ADC values plus calibration coefficients into real-world units (°C, %RH, hPa). The math uses `int32_t` and `int64_t` intermediates with careful sign handling; copied verbatim from the official reference driver.

**Live readings confirm:** temperature ~28 °C (sensor self-heats ~2 °C above ambient), humidity ~65% RH, pressure ~998 hPa (consistent with Friedberg elevation). Breathing on the sensor pushes humidity above 80% within two samples, validating that real environmental coupling works.

<p align="center">
  <img src="images/day04-bme280-readings/01-putty-live-readings.png" width="700">
  <br>
  <em>Continuous BME280 readings — temperature, humidity, and pressure printed every second over UART.</em>
</p>

<details>
<summary>Driver files and validation</summary>

<p align="center">
  <img src="images/day04-bme280-readings/02-bme280-C_file.png" width="700">
  <br>
  <em>BME280 driver (<code>bme280.c</code>) showing the algorithm, with live readings in PuTTY confirming the math produces valid real-world values.</em>
</p>

</details>

#### Day 5 (2026-05-19) — Temperature state classification and transition detection

Built a three-state safety classifier on top of the BME280 driver. Temperature thresholds: `≤ 30 °C → NORMAL`, `30 – 45 °C → WARNING`, `> 45 °C → CRITICAL`. Implemented in three composable pieces:

1. **`classify_state(float)`** — pure function mapping temperature to a `SystemState` enum value. No side effects; trivially testable. Threshold cascade evaluated most-severe-first to prevent overlap bugs.
2. **Edge-triggered transition detection** — `current_state` vs `previous_state` comparison in the main loop. State-change events are logged prominently with `*** STATE CHANGE: X -> Y ***`, while steady-state samples log normally. Critically, `previous_state` is only updated on detected transitions — the same edge-detection pattern used by debouncers, ISRs, and AUTOSAR's DEM module.
3. **State-driven feedback** — the LD2 blink rate is derived from the current state (`1 Hz` calm / `4 Hz` warning / `10 Hz` alarm) via `HAL_Delay(blink_interval_ms(state))`. The blink itself becomes a visual state indicator without needing external hardware.

Validated experimentally: pinching the sensor between fingers heats it past 30 °C → WARNING. A phone flashlight held against it pushes past 45 °C → CRITICAL. Both transitions and the reverse directions logged cleanly.

This is the same classify → detect-edge → react pattern that Node A will use in Week 2 to decide when to emit CAN state-change events.

<p align="center">
  <img src="images/day05-state-machine/03-transition-warning-to-critical.png" width="700">
  <br>
  <em>WARNING → CRITICAL transition at 45.4 °C — the threshold crossing is logged with full sensor context plus a prominent state-change marker.</em>
</p>

<details>
<summary>Other states and transitions</summary>

<p align="center">
  <img src="images/day05-state-machine/01-normal-state-steady.png" width="700">
  <br>
  <em>Steady NORMAL state at room temperature — uniform output, slow blink rate.</em>
</p>

<p align="center">
  <img src="images/day05-state-machine/02-transition-normal-to-warning.png" width="700">
  <br>
  <em>NORMAL → WARNING transition triggered by finger contact heating the sensor past 30 °C.</em>
</p>

<p align="center">
  <img src="images/day05-state-machine/04-cubeide-code-warning-state.png" width="700">
  <br>
  <em>Main loop code with state-aware printf and state-derived blink interval, alongside live WARNING-state output.</em>
</p>

</details>

#### Day 6 (2026-05-20) — Modular refactor and hysteresis

Two clean-up tasks before introducing CAN complexity next week.

**Refactor.** Extracted the state-machine logic out of `main.c` into its own module: `state_machine.h` (public API, threshold definitions, enum) and `state_machine.c` (implementations). As of Day 6 the codebase has three clear layers: hardware drivers (`bme280.*`), policy (`state_machine.*`), and orchestration (`main.c`). Same architectural pattern as Day 4's BME280 driver — one file per responsibility.

**Hysteresis.** Day 5's threshold-based classifier had a subtle flaw: temperature wobbling near a boundary (e.g. 29.9 ↔ 30.1 °C) would rapidly toggle `NORMAL ↔ WARNING` many times per second. In a real safety system this would generate runaway CAN traffic and bury operators in alarm spam. Fix: separate rising and falling thresholds with a 2 °C deadband.

- `NORMAL → WARNING` triggers at **30.0 °C** (rising)
- `WARNING → NORMAL` triggers at **28.0 °C** (falling) — must drop 2 °C below entry point
- `WARNING → CRITICAL` triggers at **45.0 °C** (rising)
- `CRITICAL → WARNING` triggers at **43.0 °C** (falling)

Implementation: `classify_state()` gained a second argument (`current` state) and a `switch` on it — different threshold logic applies depending on which state you're already in. This is the structure used by industrial thermostats, ECU temperature monitors, and AUTOSAR's diagnostic event qualifiers.

Validated experimentally with two clean transitions: room temperature → finger-warmed past 30 °C → flashlight-heated past 45 °C, with hysteresis holding cleanly at both boundaries (no flicker).

<p align="center">
  <img src="images/day06-refactor-hysteresis/03-transition-warning-to-critical.png" width="700">
  <br>
  <em>WARNING → CRITICAL transition at 45.24 °C — sustained CRITICAL state without flapping back down through the 43 °C exit threshold.</em>
</p>

<details>
<summary>Other transitions and module layout</summary>

<p align="center">
  <img src="images/day06-refactor-hysteresis/01-normal-steady-with-module.png" width="700">
  <br>
  <em>Steady NORMAL state at room temperature (~25 °C). Project Explorer (left) shows the new <code>state_machine.c</code> file alongside <code>bme280.c</code>; Outline panel (right) shows <code>state_machine.h</code> contents.</em>
</p>

<p align="center">
  <img src="images/day06-refactor-hysteresis/02-transition-normal-to-warning.png" width="700">
  <br>
  <em>NORMAL → WARNING transition at 30.48 °C, followed by sustained WARNING state with rising temperature (31.03 → 31.84 °C).</em>
</p>

</details>

#### Day 7 (2026-05-22) — CAN1 peripheral enabled, loopback validation

First CAN milestone. Enabled CAN1 in CubeMX and validated the full TX/RX path in **loopback mode** — the peripheral internally wires its TX line back to its own RX input, bypassing the transceiver and external bus. This lets us exercise the filter, mailboxes, ISR path, and bit timing without needing a second board or wired bus.

**Build sequence:**
1. Enabled CAN1 in CubeMX, mapped to the available pins on this specific Nucleo board layout (`CAN_MODE_LOOPBACK`, ~500 kbit/s effective). Mapping required board-specific verification — the headline RM pins weren't physically accessible the way the datasheet suggested.
2. NVIC: enabled `CAN1 RX0 interrupt` so frame-arrival events drive an ISR rather than polling.
3. Wrote the test fixture: `CAN_TestInit()` configures an accept-all filter (mask = 0), starts the peripheral, activates the FIFO0 notification, and sets up a `tx_header` for standard 11-bit ID `0x123` with 4-byte payload.
4. Main loop: each iteration increments a counter, calls `HAL_CAN_AddTxMessage` (non-blocking — frame goes to a hardware mailbox and transmits in the background), waits 50 ms, then checks `can_rx_flag` set by the ISR callback.

**Receive path** is interrupt-driven. The hardware FIFO0 raises an IRQ when a frame arrives; CubeMX's `CAN1_RX0_IRQHandler` calls `HAL_CAN_IRQHandler`, which calls the **weak symbol** `HAL_CAN_RxFifo0MsgPendingCallback` — overridden in `main.c` to pull the frame via `HAL_CAN_GetRxMessage` and raise a flag. This is the standard HAL extension pattern. The `volatile` qualifier on the flag is essential — without it the compiler may cache the loop-side read in a register and miss the ISR update.

**Result:** every transmitted frame round-trips through the peripheral and arrives intact at the RX FIFO. Counter increments monotonically, all data bytes match, "OK match" on every cycle.

<p align="center">
  <img src="images/day07-can-loopback/02-putty-loopback-frames.png" width="700">
  <br>
  <em>CAN loopback test: TX increments counter, the ISR callback fires for each frame, RX data matches TX exactly. Eight consecutive successful round trips.</em>
</p>

<details>
<summary>Code context and hardware</summary>

<p align="center">
  <img src="images/day07-can-loopback/01-loopback-success-with-code.png" width="700">
  <br>
  <em>CubeIDE Project Explorer shows the new <code>can.c</code> driver alongside <code>bme280.c</code>, <code>state_machine.c</code>; Outline panel shows the CAN handle types and ISR callback registration.</em>
</p>

<p align="center">
  <img src="images/day07-can-loopback/03-hardware-setup.jpg" width="500">
  <br>
  <em>Hardware setup at end of Day 7 — Nucleo-F446RE with BME280 sensor (I2C, four-wire ribbon at right) and SN65HVD230 CAN transceiver (blue board at bottom-left, wired to STM32 but with CANH/CANL unconnected since loopback mode bypasses the transceiver internally). The transceiver wiring is staged ahead of Day 8's two-node bus setup.</em>
</p>

</details>

#### Day 8 (2026-06-01) — Two-board CAN bus, transceivers carrying real signals

The transition from "peripheral validated" to "real network." Switched CAN1 from loopback to **Normal mode**, wired two SN65HVD230 transceivers via CANH/CANL with **120 Ω termination at each end of the bus**, and built a second Nucleo as a dedicated receiver project. Frames now physically leave the transmitting chip, traverse the differential bus, get ACKed by the receiver, and arrive intact.

**Build sequence:**
1. Reconfigured the sender's CubeMX: Operating Mode → Normal, bit timing tuned to **500 kbit/s** (Prescaler 6, TimeSeg1 11TQ, TimeSeg2 2TQ on 42 MHz APB1). Pins CAN1_TX = PA12, CAN1_RX = PA11.
2. Wired both transceivers identically: VCC↔3V3, GND↔GND, TXD↔CAN1_TX, RXD↔CAN1_RX, RS↔GND (high-speed mode). CANH↔CANH and CANL↔CANL between transceivers, with one 120 Ω resistor across CANH-CANL at each end of the bus.
3. Created `CAN_Receiver_Node` as a separate CubeIDE project (sibling to the sender). Same Workflow A, same per-peripheral file generation, Keep User Code on. Configured CAN1 with identical bit timing to the sender — critical for the two nodes to agree on bit boundaries.
4. Wrote a minimal receiver: `_write` retarget for UART, `CAN_ReceiverInit` with accept-all filter, weak-symbol RX callback override, main loop that prints every received frame with HAL_GetTick timestamp and a monotonic frame counter.

**Why termination matters.** CAN runs as a differential signal on a twisted pair (or any pair). The bus has a characteristic impedance near 120 Ω. Without termination resistors at each end, the signal reflects off the open wire ends back into the bus, creating standing waves that corrupt the differential. Symptom of forgetting termination: TX failures with `HAL_CAN_GetError` returning `0x40` (ACK error) — the receiver can't read the bits cleanly, so it can't ACK.

**Why Normal mode needs two nodes.** Unlike loopback, Normal mode requires at least one other node on the bus to acknowledge each transmission within the ACK slot. A frame sent into an empty bus fails after the automatic retransmission count is exhausted, and the peripheral eventually transitions to bus-off. With the receiver running and ACKing, the sender's transmissions succeed reliably.

**Result:** Node A transmits a counter-payload frame with ID `0x123`, DLC 4. Node B receives every frame in sequence, no gaps, payload matches exactly. From cold boot, first transmission to first reception completes in 8 ms.

<p align="center">
  <img src="images/day08-two-board-can/01-dual-projects-with-output.png" width="900">
  <br>
  <em>Both projects open in the same CubeIDE workspace (<code>CAN_Sender_Node</code> and <code>CAN_Receiver_Node</code>) with their PuTTY consoles. Left console (COM4 — sender): full boot sequence, CAN init diagnostics, then continuous TX. Right console (COM6 — receiver): independent boot banner, then RX frames matching the sender's payload frame-for-frame starting at 8 ms after Node B comes online.</em>
</p>

<details>
<summary>CubeMX configuration and close-up of the dual-console output</summary>

<p align="center">
  <img src="images/day08-two-board-can/03-cubemx-can1-normal-pinout.png" width="900">
  <br>
  <em>CubeMX CAN1 configuration on Nucleo-F446RE: <strong>CAN1_TX → PA12, CAN1_RX → PA11</strong>, Operating Mode <strong>Normal</strong>, Baud Rate 500 kbit/s. Identical configuration applied to both Sender and Receiver projects ensures the two nodes agree on bit timing.</em>
</p>

<p align="center">
  <img src="images/day08-two-board-can/02-putty-dual-tx-rx.png" width="900">
  <br>
  <em>Close-up of both PuTTY windows showing the full bring-up sequence: sender's init diagnostics (all <code>HAL_OK</code>, CAN error 0x00000000), then steady TX (left); receiver's independent boot, then sequential RX frames (right). Counter increments in lockstep on both sides.</em>
</p>

</details>

## Skills demonstrated

*(this section grows as work progresses)*

### Embedded C
- Bitmasks and bitwise operators (`<<`, `^=`, `|=`, `&= ~`); `volatile` keyword for memory-mapped I/O
- `enum` / `typedef` discipline for self-documenting state types
- Modular architecture: separation into hardware drivers (`bme280.*`), state policy (`state_machine.*`), and application orchestration (`main.c`)
- Syscall retargeting (`_write`) to redirect `printf` to UART
- Fixed-point compensation math: `int32_t` / `int64_t` arithmetic per Bosch datasheet specification
- Modular architecture: separation into hardware drivers (`bme280.*`), state policy (`state_machine.*`), and application orchestration (`main.c`)

### STM32 / HAL
- HAL programming — GPIO, UART, I2C; CAN coming
- CMSIS register-level access (`BSRR`, `ODR`) — atomic vs read-modify-write
- STM32CubeMX → STM32CubeIDE workflow with peripheral-per-file code generation
- `.ioc` regeneration discipline — adding peripherals mid-project (I2C on Day 3) without losing user code

### Communication protocols
- I2C bus: 7-bit addressing, master-slave model, bus scanning, register-based sensor protocols
- UART (115200 8N1): serial debugging via ST-Link virtual COM port
- CAN bus protocol: 11-bit standard frame format, mailbox-based TX, FIFO-based RX, hardware filtering (ID/mask), bit timing configuration
- CAN loopback mode for peripheral validation without external bus
- CAN Normal mode operation: differential signaling over CANH/CANL, ACK-based frame validation, multi-node bus
- CAN bus termination: 120 Ω resistors to match characteristic impedance and prevent signal reflections
- Bit-timing matching across nodes (Prescaler, BS1, BS2) at 500 kbit/s

### Sensor integration
- BME280 wiring (I2C + 3.3V power), chip ID verification pattern
- Factory calibration data handling (compensation coefficients stored in non-volatile chip memory)
- Float-formatted `printf` output via `-u _printf_float` linker flag

### Embedded design patterns
- State-machine design: threshold classification, edge-triggered transition detection (the foundational pattern for CAN event emission and AUTOSAR DEM-style fault logging)
- Hysteresis: asymmetric rising/falling thresholds to prevent state flapping near boundaries (industrial controller standard practice)
- Hysteresis: asymmetric rising/falling thresholds to prevent state flapping near boundaries (industrial controller standard practice)
- HAL weak-symbol callbacks (`HAL_CAN_RxFifo0MsgPendingCallback`) — overriding default empty implementations to handle peripheral events
- ISR-to-main-loop communication via `volatile` flag variables (mandatory to prevent compiler caching)
- Multi-project firmware design: separate CubeIDE projects for distinct nodes, each with its own peripheral configuration and main loop

### Debugging and measurement
- Live UART logging during execution; timing measurement via `HAL_GetTick()`
- Quantitative analysis of blocking I/O overhead (printf cost ≈ 2–3 ms at 115200 baud)
- Diagnostic instrumentation via `HAL_CAN_GetError()` and register inspection (MCR/MSR) when peripheral init failed silently
- Board-specific pin discovery — verifying datasheet pinout claims against actual Nucleo header accessibility
- Dual-PuTTY workflow for two-node embedded systems: independent ST-Link VCP enumeration (COM4 + COM6) for simultaneous TX/RX observation

## Author

Swayam Jakhalekar — M.Sc. Control, Computer and Communications Engineering
Technische Hochschule Mittelhessen, Friedberg