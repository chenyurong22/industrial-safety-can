#include "state_machine.h"

SystemState classify_state(float temp_c, SystemState current)
{
    /* Hysteresis: thresholds depend on which state we're currently in.
     * Rising edges (entering more severe state) use one threshold;
     * falling edges (entering less severe state) use a lower one.
     * The gap absorbs noise around the boundaries. */

    switch (current) {
        case STATE_NORMAL:
            if (temp_c > TEMP_NORMAL_TO_WARNING) return STATE_WARNING;
            break;

        case STATE_WARNING:
            if (temp_c > TEMP_WARNING_TO_CRITICAL) return STATE_CRITICAL;
            if (temp_c < TEMP_WARNING_TO_NORMAL)   return STATE_NORMAL;
            break;

        case STATE_CRITICAL:
            if (temp_c < TEMP_CRITICAL_TO_WARNING) return STATE_WARNING;
            break;
    }

    return current;  /* no transition condition met — stay put */
}

const char *state_name(SystemState state)
{
    switch (state) {
        case STATE_NORMAL:   return "NORMAL  ";
        case STATE_WARNING:  return "WARNING ";
        case STATE_CRITICAL: return "CRITICAL";
        default:             return "UNKNOWN ";
    }
}

uint32_t blink_interval_ms(SystemState state)
{
    switch (state) {
        case STATE_NORMAL:   return 1000;
        case STATE_WARNING:  return 250;
        case STATE_CRITICAL: return 100;
        default:             return 1000;
    }
}