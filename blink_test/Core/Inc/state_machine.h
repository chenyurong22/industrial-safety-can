#ifndef INC_STATE_MACHINE_H_
#define INC_STATE_MACHINE_H_

#include <stdint.h>

/* System operating states based on temperature */
typedef enum {
    STATE_NORMAL = 0,
    STATE_WARNING,
    STATE_CRITICAL
} SystemState;

/* Threshold definitions (with hysteresis) */
#define TEMP_NORMAL_TO_WARNING    30.0f   /* rising edge */
#define TEMP_WARNING_TO_NORMAL    28.0f   /* falling edge — 2°C hysteresis */
#define TEMP_WARNING_TO_CRITICAL  45.0f   /* rising edge */
#define TEMP_CRITICAL_TO_WARNING  43.0f   /* falling edge — 2°C hysteresis */

/* Public API */
SystemState classify_state(float temp_c, SystemState current);
const char *state_name(SystemState state);
uint32_t blink_interval_ms(SystemState state);

#endif /* INC_STATE_MACHINE_H_ */