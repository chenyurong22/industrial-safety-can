# Requirements Specification — Industrial Safety Monitoring CAN Network

This document captures the requirements the system was built to satisfy, retrofitted
as an explicit, traceable specification. Each requirement has a stable ID, a
`shall`-statement, a rationale, and a verification method with the evidence that
demonstrates it. IDs are referenced from commit messages and the build log to give
lightweight V-model traceability from requirement → implementation → evidence.

**Verification legend:** **T** = Test (hardware demonstration) · **A** = Analysis ·
**I** = Inspection (code/config review)

Requirement ID scheme: `REQ-F-nn` functional · `REQ-I-nn` integrity · `REQ-S-nn` safety.

---

## Functional requirements

| ID | Requirement | Rationale | Verification |
|----|-------------|-----------|--------------|
| REQ-F-01 | Node A shall acquire temperature from the BME280 sensor over I2C at a period of 100 ms. | Periodic sensing at a fixed rate is the basis of deterministic condition monitoring. | **T** — live UART readings on a 100 ms cadence (Day 4, Day 10). |
| REQ-F-02 | Node A shall classify the measured temperature into one of three states: NORMAL, WARNING, CRITICAL. | Discrete safety states drive downstream actuation and operator indication. | **T** — transitions triggered by heating the sensor across thresholds (Day 5). |
| REQ-F-03 | State classification shall apply hysteresis (a 2 °C deadband between rising and falling thresholds). | Prevents rapid state flapping near a threshold, which would generate alarm/traffic spam. | **T** — sustained state without flicker at boundary crossings (Day 6). |
| REQ-F-04 | Node A shall transmit a sensor frame (CAN ID `0x123`, DLC 8) containing counter, temperature, state, and CRC every 100 ms. | Defines the primary data-path contract between the nodes. | **T** / **I** — decoded frames on the bus (Day 9); frame format review. |
| REQ-F-05 | The CAN bus shall operate at 500 kbit/s with 120 Ω termination at each physical end. | Matches the bus's characteristic impedance to prevent reflections; standard automotive rate. | **T** / **A** — clean two-node exchange (Day 8); bit-timing analysis (42 MHz APB1, 75 % sample point). |
| REQ-F-06 | Node B shall receive and decode sensor frames using interrupt-driven reception (RX FIFO notification). | Interrupt-driven RX avoids polling latency and CPU waste, and scales to multiple message IDs. | **T** / **I** — every frame received in sequence (Day 8/9); ISR callback review. |
| REQ-F-07 | Node B shall drive a servo actuator to a position that reflects the current system state. | Demonstrates a closed loop from sensing on one node to actuation on another (cooling-valve analogue). | **T** — servo position tracks state changes (Day 11). |
| REQ-F-08 | A PC-side logger shall passively monitor the bus in listen-only mode and decode all frames without perturbing the network. | An analysis tool must observe without altering the system under test. | **T** — clean capture with Node B loss count unchanged by the logger's presence (Day 13). |

## Integrity requirements

| ID | Requirement | Rationale | Verification |
|----|-------------|-----------|--------------|
| REQ-I-01 | Node A shall append an 8-bit CRC (polynomial `0x07`) computed over payload bytes 0–6 to each sensor frame. | Application-layer integrity check independent of the CAN protocol's own CRC. | **I** / **T** — shared `crc8.c` compiled into both nodes; CRC byte present per frame (Day 9). |
| REQ-I-02 | Node B shall recompute the CRC-8 on each received frame, reject frames whose CRC mismatches, and count the failures. | Corruption must be detected and rejected before frame contents are trusted. | **T** — `crcErr` counter, 0 under normal operation (Day 9). |
| REQ-I-03 | Node A shall include a rolling 8-bit message counter that increments once per sensor frame and wraps at 256. | Enables the receiver to detect missing or duplicated frames. | **I** / **T** — counter increments 1:1 on the bus (Day 9). |
| REQ-I-04 | Node B shall detect lost frames by counter-gap analysis, computed correctly across the 255 → 0 wrap boundary. | Frame loss is a distinct failure mode from corruption and must be accounted separately. | **T** — gap correctly reported on a real mid-stream loss (Day 9, Day 11). |
| REQ-I-05 | Node B shall maintain and report running statistics: received, lost, and CRC-error counts. | A live integrity dashboard is the basis for AUTOSAR DEM-style fault qualification. | **T** — per-frame `recv` / `lost` / `crcErr` over UART (Day 9). |
| REQ-I-06 | The PC-side logger shall independently re-implement CRC-8 validation and counter-gap detection to cross-check Node B. | An independently-written third check makes a shared firmware bug detectable. | **T** — logger and Node B agree on loss/CRC counts; a wrap yields gap 0, a reboot is a known ambiguity (Day 13). |

## Safety requirements

| ID | Requirement | Rationale | Verification |
|----|-------------|-----------|--------------|
| REQ-S-01 | Node A shall transmit a heartbeat frame (CAN ID `0x100`) every 200 ms, on a schedule independent of the sensor data path. | Liveness must be provable independently of whether data frames are flowing. | **T** — heartbeats on a clean 200 ms grid, decoupled from 100 ms sensor frames (Day 10). |
| REQ-S-02 | Node B shall detect the absence of heartbeats within 500 ms of the last received heartbeat. | Bounded-time fault detection is the core of a safety supervisor; 500 ms ≈ 2.5× the heartbeat period tolerates one missed beat. | **T** — watchdog trips at ~501–502 ms on bus disconnect (Day 11, Day 12). |
| REQ-S-03 | On heartbeat timeout, Node B shall enter the safe state exactly once per fault (edge-triggered) and increment a trip counter. | Prevents repeated re-entry every loop iteration; the trip counter records fault history. | **T** — single `SAFE STATE … trips=n` per fault (Day 11). |
| REQ-S-04 | In the safe state, Node B shall drive the servo to a defined fail-safe position that is distinct from every normal operating position. | "Safe" must never be confused with a normal set-point; the actuator must reach a known defined state. | **T** — servo drives to the distinct fail-safe angle on trip (Day 11). |
| REQ-S-05 | Node B shall provide a visible fault indication (fast-blinking LED) while in the safe state. | Local operator indication of an active fault, independent of the serial console. | **T** — LD2 fast-blink during safe state (Day 11). |
| REQ-S-06 | Node B shall automatically recover to normal operation on the first heartbeat received after the fault condition clears. | Unattended systems must self-recover without human intervention once the fault is gone. | **T** — one-shot `RECOVERED` and resumed actuation on bus restore (Day 11). |
| REQ-S-07 | Node B shall suppress safe-state entry during a startup grace period of 1 s after boot. | Prevents a spurious trip at power-on before the sender's first heartbeat can arrive. | **T** — no trip during grace window at boot (Day 11). |
| REQ-S-08 | Servo actuation shall be rate-limited (stepped movement) to bound inrush current on the shared power rail. | Large single commanded swings brown out the MCU on the USB-fed 5 V rail; rate limiting keeps the demo stable. | **T** / **A** — stable actuation with rate limiting; brown-out reproduced without it (Day 11 hardware note). |

---

## Traceability notes

- The two independent fault-detection mechanisms cover complementary failure spaces:
  **REQ-I-04** (counter gaps) catches *partial* loss from an otherwise live stream;
  **REQ-S-02** (heartbeat watchdog) catches *total* loss (the stream stopping entirely).
- **REQ-I-06** documents a known limitation: a Node A reboot and mass frame loss are
  currently indistinguishable at the logger, because the heartbeat uptime/counter bytes
  wrap. The identified fix is a dedicated boot-counter in a reserved frame byte —
  tracked as a future firmware enhancement, not yet implemented.
- Day 12 provides multi-layer evidence for the safety chain: the physical-layer logic
  analyzer trace (cause), Node A NACK errors (propagation), and Node B safe-state entry
  (containment) were captured simultaneously, cross-verifying REQ-S-02 through REQ-S-04.