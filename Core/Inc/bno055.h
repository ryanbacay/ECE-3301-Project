#ifndef BNO055_H
#define BNO055_H
#include "stm32g4xx_hal.h"
typedef struct {
    float heading_deg, roll_deg, pitch_deg;
    float mag_x_uT, mag_y_uT, mag_z_uT;
    uint8_t calibration; /* Bits 7:6 system, 5:4 gyro, 3:2 accel, 1:0 mag. */
} BNO055_Data;
HAL_StatusTypeDef BNO055_Init(I2C_HandleTypeDef *bus);
HAL_StatusTypeDef BNO055_Read(I2C_HandleTypeDef *bus, BNO055_Data *out);
HAL_StatusTypeDef BNO055_ClearInterrupt(I2C_HandleTypeDef *bus);
#endif
