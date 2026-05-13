\# Industrial Safety Monitoring CAN Network



A two-node CAN bus safety system built on STM32, demonstrating embedded safety patterns used in automotive and industrial control: message integrity (CRC, counters), heartbeat/watchdog monitoring, and deterministic safe-state transitions. Python-based CAN traffic logger included for analysis.



\*\*Project status:\*\* 🚧 In progress — Week 1 of 3



\## Goals



\- Build a working two-node CAN safety network on real hardware

\- Demonstrate AUTOSAR-flavoured patterns (DEM-like diagnostics, COM-like validation, WdgM-like watchdog) in plain HAL code

\- Produce quantitative timing analysis with a Python logger

\- Document the build day-by-day as a learning record



\## Hardware



\- 2× STM32 Nucleo-F446RE

\- 2× SN65HVD230 CAN transceivers

\- 1× BME280 temperature/humidity sensor (I2C)

\- 1× SG90 servo motor

\- 1× Innomaker USB-CAN adapter for PC-side logging

\- 120 Ω termination resistors, breadboard, jumper wires



\## Repository structure

industrial-safety-can/

├── blink\_test/         # Day 1: toolchain validation, LED blink

└── ...                 # (more sub-projects added as work progresses)



\## Build log



\### Week 1 — STM32 basics, I2C sensor, UART debugging



\- \*\*Day 1\*\* (2026-05-13) — Toolchain setup complete. STM32CubeMX → STM32CubeIDE workflow validated using Board Selector for Nucleo-F446RE. LED blink (PA5) working using both HAL (`HAL\_GPIO\_TogglePin`) and CMSIS register-level (`GPIOA->BSRR`) approaches. Explored bitmask fundamentals and the difference between read-modify-write (`|=`) and atomic single-write (`BSRR`).



<!-- Append new entries below as work progresses -->



\## Skills demonstrated



\*(this section grows as work progresses)\*



\- STM32 HAL programming — GPIO so far; UART, I2C, CAN coming

\- CMSIS register-level access (`BSRR`, `ODR`)

\- Embedded C: bitmasks, bitwise operators, `volatile`

\- STM32CubeMX → STM32CubeIDE workflow with peripheral-per-file code generation



\## Author



Swayam Jakhalekar — M.Sc. Control, Computer and Communications Engineering

Technische Hochschule Mittelhessen, Friedberg

