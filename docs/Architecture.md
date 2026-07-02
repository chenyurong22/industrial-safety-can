# Architecture

*Industrial Safety Monitoring CAN Network*

This document describes the software architecture, protocol, timing, scheduling, and
control logic of the two-node system. It is the technical reference behind the day-by-day
build log in the main [README](../README.md); read this for *how it is structured and
why*, and the build log for *how it was developed*.

## 1. System overview

Two STM32 Nucleo-F446RE boards act as peers on a shared, terminated CAN bus:

- **Node A (Sender)** senses temperature, classifies it into a safety state, and
  transmits sensor and heartbeat frames with application-layer integrity.
- **Node B (Receiver / Supervisor)** validates incoming frames, drives a servo actuator
  according to state, and supervises Node A's liveness with a heartbeat-timeout watchdog
  that forces a fail-safe state.

A PC-side Python logger taps the bus in listen-only mode for independent analysis. The
three observers (sender, receiver, logger) cross-check each other.

```
        NODE A  (Sender / Sensor)                       NODE B  (Receiver / Supervisor)
        ┌───────────────────────┐                       ┌───────────────────────────┐
        │ BME280  (I2C1)         │                       │ RX ISR demultiplex by ID  │
        │   ↓                    │                       │   ↓                       │
        │ state_machine          │                       │ CRC-8 validation          │
        │  (hysteresis)          │                       │   ↓                       │
        │   ↓                    │                       │ counter / lost-frame check│
        │ crc8 + rolling counter │                       │   ↓                       │
        │   ↓                    │                       │ heartbeat watchdog (500ms)│
        │ CAN TX:                │                       │   ↓                       │
        │  0x123 sensor  (100ms) │                       │ safe-state logic          │
        │  0x100 heartbeat(200ms)│                       │   ↓                       │
        └───────────┬───────────┘                       │ servo (TIM3_CH1 PWM)      │
                    │                                    └─────────────┬─────────────┘
      ══════════════╪═════════════════ CAN BUS (500 kbit/s) ══════════╪══════════════
       120 Ω term ──┤                          │                       ├── 120 Ω term
                    │                 ┌─────────┴──────────┐            │
                    │                 │ Python logger      │            │
                    │                 │ (listen-only tap)  │            │
                    │                 └────────────────────┘            │
```

## 2. Software architecture

Both firmware projects follow a three-layer separation: hardware drivers, policy, and
orchestration. Shared modules (`crc8`, `state_machine`) are compiled identically into
both nodes so their behaviour cannot diverge.

### Node A (Sender) modules

| Module | Responsibility |
|--------|----------------|
| `bme280.c/.h` | I2C sensor driver — calibration, compensation, raw→engineering-unit conversion |
| `state_machine.c/.h` | Temperature → state classification with hysteresis; state naming |
| `crc8.c/.h` | CRC-8 (poly 0x07) over the frame payload — **shared with Node B** |
| `main.c` | Orchestration: tick-scheduled sensor + heartbeat TX, frame packing |

### Node B (Receiver) modules

| Module | Responsibility |
|--------|----------------|
| `crc8.c/.h` | Same CRC-8 as Node A — independent re-validation of received frames |
| `main.c` | Orchestration: RX demux, integrity check, watchdog, safe state, servo control |

### Node B servo control interface

Two functions abstract the PWM actuator:

- `servo_write(pulse_us)` — immediate, un-ramped write; sets `TIM3_CH1` compare and syncs
  the tracked position `servo_current_us`.
- `servo_move_to(target_us)` — rate-limited move: steps from the current position toward
  the target in `SERVO_STEP_US` (20 µs) increments with `SERVO_STEP_DELAY` (5 ms) between
  steps, bounding inrush current (see §7).

## 3. CAN protocol

Standard 11-bit identifiers, 500 kbit/s, two message types. IDs are chosen so the
heartbeat (`0x100`) wins arbitration over the sensor frame (`0x123`) — the liveness
signal has priority over data, which is the correct precedence for a safety network.

### Sensor frame — ID `0x123`, DLC 8

| Byte | 0 | 1 | 2 | 3 | 4–6 | 7 |
|------|---|---|---|---|-----|---|
| Field | Counter | Temp int (`int8`) | Temp frac (×100) | State enum | Reserved (`0x00`) | CRC-8 |

State enum: `0 = NORMAL`, `1 = WARNING`, `2 = CRITICAL`. CRC-8 (poly `0x07`, init `0x00`)
is computed over bytes 0–6 and placed in byte 7 — an **application-layer** check on top of
the CAN protocol's own CRC-15, catching corruption at the software boundary (e.g. a bad
byte assembled before transmission) that the physical-layer CRC would not.

Bytes 4–6 are reserved (currently `0x00`) — headroom for humidity/pressure, and the
intended home for a **boot-counter** to resolve the reboot-vs-loss ambiguity noted in
the requirements.

### Heartbeat frame — ID `0x100`, DLC 2

| Byte | 0 | 1 |
|------|---|---|
| Field | Rolling counter | Coarse uptime (`tick / 100`) |

The heartbeat carries no sensor data by design — its only job is to prove Node A is alive,
independent of the data path.

### Reception filtering and demultiplexing

Node B uses an accept-all hardware filter (mask = 0) so both IDs reach RX FIFO0, and
demultiplexes **in software** inside the RX ISR by branching on `hdr.StdId`: `0x100` copies
into `hb_rx_data[]` and raises `hb_rx_flag`; anything else copies into `rx_data[]` and raises
`can_rx_flag`. Copying into separate buffers in the ISR prevents a heartbeat and a sensor
frame from clobbering each other between arrival and main-loop processing.

## 4. Timing budget

| Event | Period | Source | Rationale |
|-------|--------|--------|-----------|
| Sensor frame | 100 ms | Node A scheduler | Responsive condition monitoring |
| Heartbeat | 200 ms | Node A scheduler | Liveness proof; 2× the sensor rate |
| Watchdog timeout | 500 ms | Node B supervisor | ≈ 2.5× heartbeat — tolerates one missed/jittered beat, still reacts promptly |
| Watchdog grace | 1000 ms | Node B (post-boot) | Suppresses a spurious trip before the first heartbeat can arrive |
| Main-loop tick | 5 ms | Node B `HAL_Delay(5)` | Watchdog/servo evaluation granularity |

**Bit timing (both nodes):** 500 kbit/s from a 42 MHz APB1 clock — prescaler 6, BS1 11 TQ,
BS2 2 TQ, SJW 1 TQ → 16 TQ/bit, **75 % sample point**. Both nodes must match this exactly
to agree on bit boundaries. (The Python logger's adapter runs a 48 MHz clock and must be
configured to the same 75 % sample point — a lesson from the Day 13 bring-up.)

Measured watchdog trip fires at ~501–502 ms — the small overshoot over the 500 ms threshold
is the 5 ms main-loop granularity.

## 5. Scheduling

### Node A — tick-based cooperative scheduler

Node A's main loop is non-blocking. Instead of `HAL_Delay()`, it reads `HAL_GetTick()` and
fires each task when its interval has elapsed:

```c
uint32_t now = HAL_GetTick();
if (now - last_sensor_tx    >= 100) { last_sensor_tx    = now; /* sensor  */ }
if (now - last_heartbeat_tx >= 200) { last_heartbeat_tx = now; /* heartbeat */ }
```

The two cadences are fully independent — the heartbeat proves liveness regardless of what
the sensor path is doing, which is the whole point of a liveness signal. This hand-rolled
cooperative scheduler is deliberately simple; replacing it with a preemptive RTOS
(FreeRTOS) is the planned subject of a later project, where these two tasks become
independently-scheduled threads with hard priority.

### Node B — event-flag main loop with interrupt-driven RX

Frame reception is interrupt-driven: the FIFO0 message-pending IRQ calls the overridden
weak callback `HAL_CAN_RxFifo0MsgPendingCallback`, which pulls the frame and raises a
`volatile` flag. The main loop, on a 5 ms tick, services those flags (integrity check,
statistics), then unconditionally evaluates the watchdog and drives the servo. The
`volatile` qualifier on the flags is mandatory — without it the compiler could cache the
loop-side read and miss the ISR update.

**A known timing characteristic (documented honestly):** `servo_move_to()` is called from
the main loop and blocks via `HAL_Delay(SERVO_STEP_DELAY)` per step. A large move — e.g.
NORMAL (1167 µs) → SAFE (2000 µs) is ~42 steps ≈ 210 ms — blocks the loop for that
duration. Received frames are not lost (they buffer in the hardware FIFO and are serviced
when the loop resumes), but their *processing* is deferred. For a bench demonstrator this
is acceptable; a production design would move actuation off the main loop (a timer-driven
ramp or an RTOS task) so supervision never pauses for actuation.

## 6. State machine

Temperature classification (Node A, `classify_state`) is **hysteretic** — the threshold
depends on the current state, so a value wobbling near a boundary does not chatter between
states. Entry (rising) and exit (falling) thresholds are separated by a deadband:

| Transition | Threshold | Direction |
|------------|-----------|-----------|
| NORMAL → WARNING | 30.0 °C | rising |
| WARNING → NORMAL | 28.0 °C | falling (2 °C deadband) |
| WARNING → CRITICAL | 45.0 °C | rising |
| CRITICAL → WARNING | 43.0 °C | falling (2 °C deadband) |

The function is a pure mapping `(temp, current_state) → new_state` with no side effects;
if no threshold is crossed it returns the current state unchanged. Node A emits a
`*** STATE ... ***` log line only on an actual transition (edge-triggered), comparing
`current_state` against `previous_state`.

On the receiver, the **actuator** state map (`servo_move_to`) is:

| State | Servo | Pulse |
|-------|-------|-------|
| HOME (boot reference) | 0° | 1000 µs |
| NORMAL | 30° | 1167 µs |
| WARNING | 60° | 1333 µs |
| CRITICAL | 90° | 1500 µs |
| **SAFE (fail-safe)** | **180°** | **2000 µs** |

The fail-safe position (180°, full-open cooling) is deliberately distinct from every
normal operating angle, so "safe" can never be confused with a normal set-point.

## 7. Safe state and fault handling

Node B's supervisor logic runs every main-loop tick:

1. **Timeout check.** `since_hb = now - last_hb_tick`. If past the grace period
   (`now > 1000 ms`) and `since_hb > 500 ms`, the watchdog condition is met.
2. **Edge-triggered entry.** Guarded by `in_safe_state` so entry runs exactly once per
   fault: increments `watchdog_trips`, logs the trip with the measured `since_hb`, and
   commands `servo_move_to(SERVO_SAFE)`. While in safe state, LD2 fast-blinks (100 ms
   toggle) as a local fault indicator.
3. **Recovery.** When heartbeats resume (`since_hb` drops back under threshold),
   `in_safe_state` clears, a one-shot `RECOVERED` line prints, and normal state-driven
   actuation resumes.
4. **Normal actuation.** Outside the fault condition, the servo tracks the received state
   byte (`rx_data[3]`) via the state→angle map above.

### Complementary fault detection

Two independent mechanisms cover different failure modes:

- **Counter-gap detection** (`crc8` + rolling counter) catches **partial** loss — some
  frames missing from an otherwise live stream. It needs incoming frames to measure gaps
  against.
- **Heartbeat watchdog** catches **total** loss — the stream stopping entirely, where gap
  detection has nothing to measure.

Neither alone is sufficient; together they cover the full loss space. The Day 12
fault-injection capture demonstrates the total-loss path (watchdog fires, gap detector
idle); the Day 9 mid-stream attach demonstrates the partial-loss path.

## 8. Power integrity (actuator)

The SG90 servo runs from the Nucleo's USB-fed 5 V rail, which cannot source the servo's
stall current through a full-range move — a large commanded swing browns out the MCU and
resets the board. Two measures keep the demonstrator stable:

- **Rate limiting** — `servo_move_to()` approaches the target in 20 µs steps every 5 ms,
  bounding inrush current.
- **Set-point capping** — actuation stays within the range the shared rail can drive.

The PWM commands the full range correctly; the constraint is electrical, not logical. A
production design would give the actuator a **dedicated 5 V supply with a common ground**
to the controller — the standard way to isolate an inductive load's current transients
from the MCU. Documented as a real constraint rather than hidden.

## 9. Known limitations and future work

- **Reboot vs. mass loss.** A Node A reboot resets the counter toward zero, indistinguishable
  from a large gap; the heartbeat's uptime/counter bytes wrap (25.6 s / 51.2 s) and so cannot
  disambiguate. Fix: a dedicated boot-counter in a reserved frame byte (4–6). *(REQ-I-06)*
- **Actuation blocks supervision.** `servo_move_to()` blocks the main loop during a ramp
  (see §5). Fix: timer-driven or RTOS-task actuation so supervision never pauses.
- **Cooperative scheduling.** Node A's tick scheduler has no preemption; a long task would
  delay the heartbeat. Migrating to FreeRTOS (a later project) gives hard task priorities.