# MISRA-C:2012 Compliance Report

*Industrial Safety Monitoring CAN Network*

Static analysis of the project's application C against the MISRA-C:2012 guidelines,
using `cppcheck` with the bundled MISRA addon. This documents the analysis scope, the
violations found, which were corrected, and which are retained as **justified
deviations** with rationale — the standard way a MISRA report is presented.

## Scope

Analysis covers **application code only** — the files written for this project:

| Node | Files analysed |
|------|----------------|
| Sender (Node A) | `main.c`, `bme280.c`, `crc8.c`, `state_machine.c` |
| Receiver (Node B) | `main.c`, `crc8.c` |

ST-generated **HAL and CMSIS** sources are excluded from findings (via a
suppressions list). Vendor-generated code carries its own deviations that are
outside this project's ownership; including it would obscure the application-level
result. Header search paths for HAL/CMSIS are still provided so cppcheck resolves
types correctly.

## Command

```
cppcheck --std=c99 --addon=misra --enable=style --inline-suppr --template=gcc \
         -DSTM32F446xx --suppressions-list=cppcheck_suppressions.txt \
         -I<node>/Core/Inc \
         -I<node>/Drivers/CMSIS/Include \
         -I<node>/Drivers/CMSIS/Device/ST/STM32F4xx/Include \
         -I<node>/Drivers/STM32F4xx_HAL_Driver/Inc \
         <node>/Core/Src/*.c  2> misra_<node>_clean.txt
```

## Result summary

| | Findings before | Findings after | Remaining are |
|---|---|---|---|
| Sender (Node A) | ~24 | 16 | 8.7 (×4) + 15.5 (×12) — all justified deviations |
| Receiver (Node B) | 5 | 1 | 8.7 (×1) — justified deviation |

All mechanically-fixable violations were corrected. Every remaining finding is a
documented, justified deviation — no unaddressed violations remain.

## Violations corrected

| Rule | Category | Description | Fix applied |
|------|----------|-------------|-------------|
| 10.4 | Required | Operands of an operator shall have the same essential type | Added `U` suffixes to integer constants (`0x80U`, `0x07U`, `8U`, `1000U`, …) and explicit `(uint8_t)` casts on shift results, which promote to `int` in C |
| 14.4 | Required | The controlling expression of `if` shall have essentially boolean type | Rewrote `if (crc & 0x80)` as `if ((crc & 0x80U) != 0U)` |
| 15.6 | Required | The body of an `if` shall be a compound statement (braces) | Added `{ }` braces to all single-statement `if` bodies in `state_machine.c` |
| 16.4 | Required | Every `switch` statement shall have a `default` clause | Added a `default: break;` to the `classify_state` state switch |

The corrected `crc8.c` and `state_machine.c` are compiled identically into both nodes.

## Justified deviations (retained)

| Rule | Category | Location | Rationale |
|------|----------|----------|-----------|
| 8.7 | Advisory | `crc8_compute`; `classify_state`, `state_name`, `blink_interval_ms` | These functions are the shared public API of their modules and are **linked into both the sender and receiver nodes** to guarantee bit-identical behaviour. cppcheck analyses one translation unit at a time and cannot see the cross-node usage, so it flags them as candidates for `static`. Making them static would break the shared-code design. Deviation accepted. |
| 15.5 | Advisory | `classify_state` threshold cascade; `state_name` and `blink_interval_ms` lookups | Rule 15.5 (single point of exit) is **advisory**. The per-case `return` statements in these small pure functions express a direct enum-to-value / threshold-to-state mapping. A forced single-exit rewrite (accumulator variable + trailing return) would add state and reduce readability without changing behaviour. Deviation accepted for clarity. |

Both deviated rules are classified **Advisory** in MISRA-C:2012 — the category the
standard explicitly permits deviating from with recorded rationale, as done here.

## Notes

- This is a static-analysis compliance exercise on a bench demonstrator, not a
  certified safety assessment. It demonstrates familiarity with the MISRA workflow:
  run the checker, fix what should be fixed, and document the rest with engineering
  rationale rather than silently suppressing findings.
- Re-running the command above reproduces the "after" state: `crc8.c` reports only
  8.7; `state_machine.c` reports only 8.7 and 15.5.