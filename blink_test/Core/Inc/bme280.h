#ifndef INC_BME280_H_
#define INC_BME280_H_

#include "main.h"
#include "i2c.h"

/* BME280 I2C address (after << 1 for HAL) */
#define BME280_I2C_ADDR    (0x76 << 1)

/* Public API */
HAL_StatusTypeDef BME280_Init(void);
HAL_StatusTypeDef BME280_Read(float *temperature_c, float *humidity_pct, float *pressure_hpa);

#endif /* INC_BME280_H_ */