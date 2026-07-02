# Hazard Analysis and Risk Assessment (HARA)

*Industrial Safety Monitoring CAN Network*

This is a lightweight, ISO 26262-inspired HARA for a bench demonstrator. It is an
**educational exercise** in the framework — identifying hazards, classifying risk, and
deriving safety goals and requirements — not a certified automotive safety case. The
system models a thermal-supervision loop: a sensor node reports temperature and liveness,
and a supervisor node drives a cooling actuator (represented by a servo) and enforces a
safe state if supervision is lost.

## Item definition

The "item" is the two-node CAN network treated as a thermal safety function: Node A senses
temperature and transmits sensor + heartbeat frames; Node B validates integrity, actuates
the cooling element according to state, and supervises Node A's liveness. The safety
function is *"drive the cooling actuator to a fail-safe (fully-open) position, and raise a
fault indication, if valid supervision of the temperature source is lost."*

## Risk classification scale

Simplified from ISO 26262-3. Each hazard is rated on three axes; the combination indicates
the relative integrity demand (shown as an ASIL-style band for illustration only).

- **Severity (S):** S0 none · S1 light · S2 severe (survivable) · S3 life-threatening
- **Exposure (E):** E1 very low · E2 low · E3 medium · E4 high
- **Controllability (C):** C1 simply controllable · C2 normally controllable · C3 difficult / uncontrollable

## Hazard analysis

| ID | Hazard (malfunction) | Effect | S | E | C | Risk band | Safety goal | Derived req. |
|----|----------------------|--------|---|---|---|-----------|-------------|--------------|
| HAZ-01 | Sensor node goes silent (hang, power loss, or severed bus) and the supervisor keeps holding the last actuator position | Cooling is no longer commanded by live data; a real over-temperature would go unmitigated | S3 | E3 | C3 | high | On loss of supervision, reach the fail-safe (fully-open cooling) state within a bounded time | REQ-S-01, REQ-S-02, REQ-S-04 |
| HAZ-02 | Corrupted sensor frame accepted as valid, giving a wrong temperature/state | Actuator driven to the wrong position for the true condition (e.g. cooling reduced during a real rise) | S3 | E2 | C3 | high | Reject frames that fail an application-layer integrity check before acting on them | REQ-I-01, REQ-I-02, REQ-I-06 |
| HAZ-03 | Silent partial frame loss — some frames dropped from an otherwise live stream | Stale actuator commands; degraded responsiveness to a changing condition | S2 | E3 | C2 | medium | Detect missing frames and account for them so degraded data is visible, not silent | REQ-I-03, REQ-I-04, REQ-I-05 |
| HAZ-04 | Spurious safe-state entry at power-on before the first heartbeat arrives | Nuisance trip; actuator forced open with no real fault, eroding trust in the safety function | S1 | E4 | C2 | low | Suppress safe-state entry until supervision has had a fair chance to establish | REQ-S-07 |
| HAZ-05 | Rapid state oscillation near a temperature threshold | Actuator chatter and alarm/traffic flooding; operator desensitised to alarms | S1 | E3 | C2 | low | Stabilise state transitions against boundary noise | REQ-F-03 |
| HAZ-06 | Actuator commanded across its full range on a shared supply, browning out the controller | Controller reset mid-operation — the supervisor itself drops out | S2 | E3 | C2 | medium | Bound actuator inrush so actuation cannot reset the controller | REQ-S-08 |

## Safety goals summary

- **SG-1 (from HAZ-01):** Loss of supervision shall lead to the fail-safe state within 500 ms.
  *Realised by* the 200 ms heartbeat (REQ-S-01), the 500 ms timeout watchdog (REQ-S-02),
  and a defined fail-safe actuator position (REQ-S-04). **Verified** at ~501 ms in the
  Day 11 and Day 12 fault-injection captures.
- **SG-2 (from HAZ-02):** No frame shall be acted upon unless its application-layer CRC-8
  validates. *Realised by* REQ-I-01/02, cross-checked independently by the PC logger
  (REQ-I-06).
- **SG-3 (from HAZ-03):** Partial frame loss shall be detected and counted, never silent.
  *Realised by* the rolling counter and gap detection (REQ-I-03/04/05).
- **SG-4 (from HAZ-04/06):** The safety function shall not create new hazards — no spurious
  trips at startup (REQ-S-07), no self-induced controller reset (REQ-S-08).

## Scope and honesty notes

- Severity/Exposure/Controllability values are **illustrative** — assigned to practise the
  classification method, not from a field study of a specific vehicle or plant.
- Complementary coverage is deliberate: SG-1 (heartbeat watchdog) catches *total* loss,
  SG-3 (counter gaps) catches *partial* loss — see the Day 12 build-log note.
- A known residual gap is documented in `REQUIREMENTS.md` (REQ-I-06): the logger cannot yet
  distinguish a sender reboot from mass loss; the fix is a dedicated boot-counter field.