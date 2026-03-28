#ifndef MPU6500_H
#define MPU6500_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

#define MPU6500_I2C_ADDRESS               0x68
#define MPU6500_I2C_ADDRESS_ALT           0x69

#define MPU6500_I2C_PORT                  I2C_NUM_1
#define MPU6500_I2C_SDA_GPIO              GPIO_NUM_32
#define MPU6500_I2C_SCL_GPIO              GPIO_NUM_33
#define MPU6500_I2C_FREQ_HZ               100000

#define MPU6500_REG_SMPLRT_DIV            0x19
#define MPU6500_REG_CONFIG                0x1A
#define MPU6500_REG_GYRO_CONFIG           0x1B
#define MPU6500_REG_ACCEL_CONFIG          0x1C
#define MPU6500_REG_ACCEL_CONFIG_2        0x1D
#define MPU6500_REG_INT_ENABLE            0x38
#define MPU6500_REG_ACCEL_XOUT_H          0x3B
#define MPU6500_REG_TEMP_OUT_H            0x41
#define MPU6500_REG_GYRO_XOUT_H           0x43
#define MPU6500_REG_PWR_MGMT_1            0x6B
#define MPU6500_REG_PWR_MGMT_2            0x6C
#define MPU6500_REG_WHO_AM_I              0x75

#define MPU6500_WHO_AM_I_VALUE            0x70
#define MPU6500_ACCEL_SENSITIVITY_2G      16384.0f
#define MPU6500_GYRO_SENSITIVITY_250DPS   131.0f
#define MPU6500_TEMP_SENSITIVITY          333.87f
#define MPU6500_TEMP_OFFSET_C             21.0f
#define MPU6500_COMPLEMENTARY_ALPHA       0.98f

typedef struct {
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
    int16_t temp;
    int16_t gyro_x;
    int16_t gyro_y;
    int16_t gyro_z;
} mpu6500_raw_data_t;

typedef struct {
    float accel_x_g;
    float accel_y_g;
    float accel_z_g;
    float gyro_x_dps;
    float gyro_y_dps;
    float gyro_z_dps;
    float temperature_c;
} mpu6500_scaled_data_t;

typedef struct {
    float pitch;
    float roll;
} mpu6500_attitude_t;

esp_err_t i2c_master_init(void);
esp_err_t mpu6500_init(void);
esp_err_t mpu6500_read_who_am_i(uint8_t *who_am_i);
esp_err_t mpu6500_read_raw(mpu6500_raw_data_t *raw_data);
esp_err_t mpu6500_read_scaled(mpu6500_scaled_data_t *scaled_data);
esp_err_t mpu6500_read_attitude(mpu6500_attitude_t *attitude);
void mpu6500_reset_attitude_filter(void);

#ifdef __cplusplus
}
#endif

#endif
