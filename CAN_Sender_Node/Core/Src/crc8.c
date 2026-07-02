#include "crc8.h"

/* Deviation (MISRA 8.7): crc8_compute is intentionally external — the same
 * object is linked into both the sender and receiver nodes to guarantee a
 * bit-identical CRC on each side, so it cannot be made static. */
uint8_t crc8_compute(const uint8_t *data, size_t length)
{
    uint8_t crc = 0x00U;

    for (size_t i = 0U; i < length; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x80U) != 0U) {
                crc = (uint8_t)((uint8_t)(crc << 1U) ^ 0x07U);
            } else {
                crc = (uint8_t)(crc << 1U);
            }
        }
    }
    return crc;
}