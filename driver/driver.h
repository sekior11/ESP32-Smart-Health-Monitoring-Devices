#ifndef DRIVER_H
#define DRIVER_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "cst816s.h"
#include "mpu6500.h"
#include "max30102.h"
#include "ble_beacon.h"

// 全局触摸屏句柄
extern cst816s_handle_t g_touch_handle;
// MAX30102 I2C句柄
extern i2c_master_dev_handle_t max30102_handle;
// I2C互斥锁
extern SemaphoreHandle_t g_i2c_mutex;

// 初始化所有驱动
esp_err_t driver_init(void);
// 初始化触摸驱动
esp_err_t touch_driver_init(void);
// 初始化MPU6500驱动
esp_err_t mpu6500_driver_init(void);
// 初始化MAX30102驱动
esp_err_t max30102_driver_init(void);
// 初始化蓝牙驱动
esp_err_t ble_driver_init(void);

#endif // DRIVER_H