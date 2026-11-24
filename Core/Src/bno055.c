#include "bno055.h"
#define ADDRESS (0x28U << 1) /* COM3 LOW; HAL takes a shifted address. */
#define TIMEOUT 100U

static HAL_StatusTypeDef read_reg(I2C_HandleTypeDef *bus, uint8_t reg,
                                  uint8_t *data, uint16_t size)
{
    return HAL_I2C_Mem_Read(bus, ADDRESS, reg, I2C_MEMADD_SIZE_8BIT,
                           data, size, TIMEOUT);
}

static HAL_StatusTypeDef write_reg(I2C_HandleTypeDef *bus, uint8_t reg,
                                   uint8_t value)
{
    HAL_StatusTypeDef result = HAL_I2C_Mem_Write(bus, ADDRESS, reg,
        I2C_MEMADD_SIZE_8BIT, &value, 1, TIMEOUT);
    HAL_Delay(2); /* Allow spacing between register writes. */
    return result;
}

static int16_t signed_word(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

HAL_StatusTypeDef BNO055_I2C_Test(I2C_HandleTypeDef *bus, uint8_t *chip_id)
{
    /* Write the page register, then read the chip ID in a second transaction. */
    if (write_reg(bus, 0x07, 0x00) != HAL_OK)
        return HAL_ERROR;

    return read_reg(bus, 0x00, chip_id, 1);
}

HAL_StatusTypeDef BNO055_Init(I2C_HandleTypeDef *bus)
{
    uint8_t id = 0;
    HAL_Delay(700); /* Sensor startup. */
    /* Select page 0 even when restarting only the MCU. */
    if (write_reg(bus, 0x07, 0x00) != HAL_OK) return HAL_ERROR;
    if (read_reg(bus, 0x00, &id, 1) != HAL_OK || id != 0xA0)
        return HAL_ERROR;
    if (write_reg(bus, 0x3D, 0x00) != HAL_OK) return HAL_ERROR;
    HAL_Delay(25); /* Operation mode -> CONFIG mode. */
    // Normal power mode
    if (write_reg(bus, 0x3E, 0x00) != HAL_OK)
        return HAL_ERROR;

    /* Remap the sensor X/Y axes for the board's physical mounting. */
    if (write_reg(bus, 0x41, 0x21) != HAL_OK)
        return HAL_ERROR;

    // Set angle units to degrees
    if (write_reg(bus, 0x3B, 0x00) != HAL_OK)
        return HAL_ERROR;

    // Switch to register page 1
    if (write_reg(bus, 0x07, 0x01) != HAL_OK)
        return HAL_ERROR;

    // Enable ACC_BSX_DRDY interrupt (bit 0)
    if (write_reg(bus, 0x10, 0x01) != HAL_OK)
        return HAL_ERROR;

    // Route ACC_BSX_DRDY to the physical INT pin
    if (write_reg(bus, 0x0F, 0x01) != HAL_OK)
        return HAL_ERROR;

    // Return to register page 0
    if (write_reg(bus, 0x07, 0x00) != HAL_OK)
        return HAL_ERROR;

    // Enter NDOF fusion mode
    if (write_reg(bus, 0x3D, 0x0C) != HAL_OK)
        return HAL_ERROR;

    HAL_Delay(25);

    return HAL_OK;
}

HAL_StatusTypeDef BNO055_Read(I2C_HandleTypeDef *bus, BNO055_Data *out)
{
    uint8_t euler[6], mag[6], cal;
    /* Publish only after all transactions succeed. */
    if (read_reg(bus, 0x1A, euler, 6) != HAL_OK) return HAL_ERROR;
    if (read_reg(bus, 0x0E, mag, 6) != HAL_OK) return HAL_ERROR;
    if (read_reg(bus, 0x35, &cal, 1) != HAL_OK) return HAL_ERROR;
    /* No additional heading offset is needed for the current mounting. */
    out->heading_deg = signed_word(euler) / 16.0f;
    if (out->heading_deg >= 360.0f) out->heading_deg -= 360.0f;
    if (out->heading_deg < 0.0f) out->heading_deg += 360.0f;
    out->roll_deg = signed_word(euler + 2) / 16.0f;
    out->pitch_deg = signed_word(euler + 4) / 16.0f;
    out->mag_x_uT = signed_word(mag) / 16.0f;
    out->mag_y_uT = signed_word(mag + 2) / 16.0f;
    out->mag_z_uT = signed_word(mag + 4) / 16.0f;
    out->calibration = cal;
    return HAL_OK;
}

HAL_StatusTypeDef BNO055_ClearInterrupt(I2C_HandleTypeDef *bus)
{
    uint8_t status;

    // Read INT_STA (page 0) to acknowledge interrupt
    return write_reg(bus, 0x3F, 0x40);
}

HAL_StatusTypeDef BNO055_GetCalibrationStatus(I2C_HandleTypeDef *bus, uint8_t *status)
{
    return read_reg(bus, 0x35, status, 1);
}

HAL_StatusTypeDef BNO055_ReadCalibrationProfile(I2C_HandleTypeDef *bus, uint8_t *profile)
{
    if (write_reg(bus, 0x3D, 0x00) != HAL_OK) return HAL_ERROR;
    HAL_Delay(25);
    HAL_StatusTypeDef result = read_reg(bus, 0x55, profile, 22);
    if (write_reg(bus, 0x3D, 0x0C) != HAL_OK) return HAL_ERROR;
    HAL_Delay(25);
    return result;
}

HAL_StatusTypeDef BNO055_ApplyCalibrationProfile(I2C_HandleTypeDef *bus, const uint8_t *profile)
{
    if (write_reg(bus, 0x3D, 0x00) != HAL_OK) return HAL_ERROR;
    HAL_Delay(25);
    for (uint8_t i = 0; i < 22; i++) {
        if (write_reg(bus, (uint8_t)(0x55U + i), profile[i]) != HAL_OK)
            return HAL_ERROR;
    }
    if (write_reg(bus, 0x3D, 0x0C) != HAL_OK) return HAL_ERROR;
    HAL_Delay(25);
    return HAL_OK;
}
