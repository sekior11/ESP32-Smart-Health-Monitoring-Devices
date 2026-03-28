#ifndef _MAX30102_H_
#define _MAX30102_H_

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c_master.h" // 使用全新的 I2C Master API

// MAX30102 7位 I2C 地址
#define MAX30102_I2C_ADDR       0x57

#define I2C_PORT_IN_USE I2C_NUM_0
// --- 寄存器地址定义 ---
#define MAX30102_INT_STAT1      0x00
#define MAX30102_FIFO_WR_PTR    0x04
#define MAX30102_OVF_COUNTER    0x05
#define MAX30102_FIFO_RD_PTR    0x06
#define MAX30102_FIFO_DATA      0x07
#define MAX30102_FIFO_CONFIG    0x08
#define MAX30102_MODE_CONFIG    0x09
#define MAX30102_SPO2_CONFIG    0x0A
#define MAX30102_LED1_PA        0x0C 
#define MAX30102_LED2_PA        0x0D 
#define MAX30102_DIE_TEMP_INT   0x1F
#define MAX30102_DIE_TEMP_FRAC  0x20
#define MAX30102_DIE_TEMP_CONFIG 0x21
#define MAX30102_PART_ID        0xFF

// --- 函数声明 ---

/**
 * @brief 将 MAX30102 挂载到已有的 I2C 总线上并初始化
 * @param bus_handle 您的主工程中已经创建好的 I2C 总线句柄
 * @param out_dev_handle 用于输出 MAX30102 的设备句柄
 * @return esp_err_t 
 */
esp_err_t max30102_init(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t *out_dev_handle);

/**
 * @brief 清空 FIFO 指针
 */
esp_err_t max30102_clear_fifo(i2c_master_dev_handle_t dev_handle);

/**
 * @brief 读取最新的 Red 和 IR 原始数据
 */
esp_err_t max30102_read_fifo_data(i2c_master_dev_handle_t dev_handle, uint32_t *red_data, uint32_t *ir_data);

/**
 * @brief 读取芯片内部温度
 */
esp_err_t max30102_read_temperature(i2c_master_dev_handle_t dev_handle, float *temp_c);

#endif // _MAX30102_H_