#ifndef CRC8_H
#define CRC8_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Compute CRC-8 over a byte buffer.
 *
 * Polynomial: 0x07 (CRC-8/SMBUS — used in SMBus PEC and I2C-based sensors)
 * Init value: 0x00
 * No final XOR.
 *
 * @param data    Pointer to input bytes
 * @param length  Number of bytes to process
 * @return        CRC-8 result
 */
uint8_t crc8_compute(const uint8_t *data, size_t length);

#endif /* CRC8_H */