#include "bme280.h"

/* Calibration coefficient storage (read once at init) */
static uint16_t dig_T1, dig_P1;
static int16_t  dig_T2, dig_T3;
static int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
static uint8_t  dig_H1, dig_H3;
static int16_t  dig_H2, dig_H4, dig_H5;
static int8_t   dig_H6;

/* Shared between temperature and pressure compensation (per datasheet) */
static int32_t t_fine;

HAL_StatusTypeDef BME280_Init(void)
{
    uint8_t buf[26];
    HAL_StatusTypeDef status;

    /* Read calibration block 1: registers 0x88..0xA1 (26 bytes) */
    status = HAL_I2C_Mem_Read(&hi2c1, BME280_I2C_ADDR, 0x88,
                              I2C_MEMADD_SIZE_8BIT, buf, 26, 100);
    if (status != HAL_OK) return status;

    dig_T1 = (uint16_t)(buf[0]  | (buf[1]  << 8));
    dig_T2 = (int16_t) (buf[2]  | (buf[3]  << 8));
    dig_T3 = (int16_t) (buf[4]  | (buf[5]  << 8));
    dig_P1 = (uint16_t)(buf[6]  | (buf[7]  << 8));
    dig_P2 = (int16_t) (buf[8]  | (buf[9]  << 8));
    dig_P3 = (int16_t) (buf[10] | (buf[11] << 8));
    dig_P4 = (int16_t) (buf[12] | (buf[13] << 8));
    dig_P5 = (int16_t) (buf[14] | (buf[15] << 8));
    dig_P6 = (int16_t) (buf[16] | (buf[17] << 8));
    dig_P7 = (int16_t) (buf[18] | (buf[19] << 8));
    dig_P8 = (int16_t) (buf[20] | (buf[21] << 8));
    dig_P9 = (int16_t) (buf[22] | (buf[23] << 8));
    dig_H1 = buf[25];

    /* Read calibration block 2: registers 0xE1..0xE7 (7 bytes) */
    status = HAL_I2C_Mem_Read(&hi2c1, BME280_I2C_ADDR, 0xE1,
                              I2C_MEMADD_SIZE_8BIT, buf, 7, 100);
    if (status != HAL_OK) return status;

    dig_H2 = (int16_t)(buf[0] | (buf[1] << 8));
    dig_H3 = buf[2];
    dig_H4 = (int16_t)((buf[3] << 4) | (buf[4] & 0x0F));
    dig_H5 = (int16_t)((buf[5] << 4) | (buf[4] >> 4));
    dig_H6 = (int8_t)buf[6];

    /* Configure: humidity oversampling x1 (0xF2 = 0x01) */
    uint8_t ctrl_hum = 0x01;
    status = HAL_I2C_Mem_Write(&hi2c1, BME280_I2C_ADDR, 0xF2,
                               I2C_MEMADD_SIZE_8BIT, &ctrl_hum, 1, 100);
    if (status != HAL_OK) return status;

    /* Configure: temp x1, pressure x1, normal mode (0xF4 = 0x27) */
    uint8_t ctrl_meas = (0x01 << 5) | (0x01 << 2) | 0x03;
    status = HAL_I2C_Mem_Write(&hi2c1, BME280_I2C_ADDR, 0xF4,
                               I2C_MEMADD_SIZE_8BIT, &ctrl_meas, 1, 100);
    if (status != HAL_OK) return status;

    /* Configure: standby 1000 ms, filter off (0xF5 = 0xA0) */
    uint8_t config = (0x05 << 5);
    status = HAL_I2C_Mem_Write(&hi2c1, BME280_I2C_ADDR, 0xF5,
                               I2C_MEMADD_SIZE_8BIT, &config, 1, 100);

    return status;
}

HAL_StatusTypeDef BME280_Read(float *temperature_c, float *humidity_pct, float *pressure_hpa)
{
    uint8_t data[8];

    /* Read 8 bytes from 0xF7: P_msb, P_lsb, P_xlsb, T_msb, T_lsb, T_xlsb, H_msb, H_lsb */
    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(&hi2c1, BME280_I2C_ADDR, 0xF7,
                                                I2C_MEMADD_SIZE_8BIT, data, 8, 100);
    if (status != HAL_OK) return status;

    int32_t adc_P = (int32_t)(((uint32_t)data[0] << 12) | ((uint32_t)data[1] << 4) | (data[2] >> 4));
    int32_t adc_T = (int32_t)(((uint32_t)data[3] << 12) | ((uint32_t)data[4] << 4) | (data[5] >> 4));
    int32_t adc_H = (int32_t)(((uint32_t)data[6] << 8)  | data[7]);

    /* Temperature compensation (Bosch reference, datasheet section 4.2.3) */
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) *
                      ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) *
                    ((int32_t)dig_T3)) >> 14;
    t_fine = var1 + var2;
    int32_t T = (t_fine * 5 + 128) >> 8;
    *temperature_c = T / 100.0f;

    /* Pressure compensation */
    int64_t p_var1 = ((int64_t)t_fine) - 128000;
    int64_t p_var2 = p_var1 * p_var1 * (int64_t)dig_P6;
    p_var2 = p_var2 + ((p_var1 * (int64_t)dig_P5) << 17);
    p_var2 = p_var2 + (((int64_t)dig_P4) << 35);
    p_var1 = ((p_var1 * p_var1 * (int64_t)dig_P3) >> 8) + ((p_var1 * (int64_t)dig_P2) << 12);
    p_var1 = (((((int64_t)1) << 47) + p_var1)) * ((int64_t)dig_P1) >> 33;
    if (p_var1 == 0) {
        *pressure_hpa = 0;
    } else {
        int64_t p = 1048576 - adc_P;
        p = (((p << 31) - p_var2) * 3125) / p_var1;
        p_var1 = (((int64_t)dig_P9) * (p >> 13) * (p >> 13)) >> 25;
        p_var2 = (((int64_t)dig_P8) * p) >> 19;
        p = ((p + p_var1 + p_var2) >> 8) + (((int64_t)dig_P7) << 4);
        *pressure_hpa = (float)p / 25600.0f;
    }

    /* Humidity compensation */
    int32_t v_x1 = t_fine - 76800;
    v_x1 = (((((adc_H << 14) - (((int32_t)dig_H4) << 20) - (((int32_t)dig_H5) * v_x1)) +
              16384) >> 15) *
            (((((((v_x1 * ((int32_t)dig_H6)) >> 10) *
                 (((v_x1 * ((int32_t)dig_H3)) >> 11) + 32768)) >> 10) +
               2097152) * ((int32_t)dig_H2) + 8192) >> 14));
    v_x1 = v_x1 - (((((v_x1 >> 15) * (v_x1 >> 15)) >> 7) * ((int32_t)dig_H1)) >> 4);
    if (v_x1 < 0) v_x1 = 0;
    if (v_x1 > 419430400) v_x1 = 419430400;
    *humidity_pct = (float)(v_x1 >> 12) / 1024.0f;

    return HAL_OK;
}