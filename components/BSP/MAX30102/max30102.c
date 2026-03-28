#include "max30102.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAX30102_NG";

/**
 * @brief 向 MAX30102 寄存器写入数据
 * 
 * 该函数用于向 MAX30102 传感器的指定寄存器写入一个字节的数据。
 * 它是 MAX30102 驱动的底层函数，被其他高层函数调用。
 * 
 * @param dev I2C 设备句柄
 * @param reg 寄存器地址
 * @param data 要写入的数据
 * @return esp_err_t 操作结果
 *         - ESP_OK: 成功写入数据
 *         - 其他错误码: 写入失败
 */
static esp_err_t max30102_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t data) {
    uint8_t write_buf[2] = {reg, data};
    return i2c_master_transmit(dev, write_buf, 2, -1);
}

/**
 * @brief 从 MAX30102 寄存器读取数据
 * 
 * 该函数用于从 MAX30102 传感器的指定寄存器读取指定长度的数据。
 * 它是 MAX30102 驱动的底层函数，被其他高层函数调用。
 * 
 * @param dev I2C 设备句柄
 * @param reg 寄存器地址
 * @param data 用于存储读取数据的缓冲区指针
 * @param len 要读取的数据长度
 * @return esp_err_t 操作结果
 *         - ESP_OK: 成功读取数据
 *         - 其他错误码: 读取失败
 */
static esp_err_t max30102_read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *data, size_t len) {
    return i2c_master_transmit_receive(dev, &reg, 1, data, len, -1);
}

/**
 * @brief 初始化 MAX30102 心率血氧传感器
 * 
 * 该函数将 MAX30102 传感器挂载到已有的 I2C 总线上，进行初始化配置，
 * 包括检查芯片 ID、软件复位和设置传感器参数。
 * 
 * @param bus_handle 已创建的 I2C 总线句柄
 * @param out_dev_handle 用于输出 MAX30102 的设备句柄
 * @return esp_err_t 操作结果
 *         - ESP_OK: 成功初始化
 *         - ESP_FAIL: 芯片 ID 错误
 *         - 其他错误码: I2C 操作失败
 */
esp_err_t max30102_init(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t *out_dev_handle) {
    esp_err_t ret;
    
    // 1. 将 MAX30102 作为一个设备添加到总线上
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MAX30102_I2C_ADDR,
        .scl_speed_hz = 400000, // 400kHz
    };
    ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, out_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "添加 MAX30102 设备到总线失败");
        return ret;
    }

    i2c_master_dev_handle_t dev = *out_dev_handle;
    uint8_t part_id;

    // 2. 检查芯片 ID
    ret = max30102_read_reg(dev, MAX30102_PART_ID, &part_id, 1);
    if (ret != ESP_OK || part_id != 0x15) {
        ESP_LOGE(TAG, "MAX30102 未找到或 PART_ID 错误 (读到: 0x%02X)", part_id);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "成功检测到 MAX30102 (新版驱动挂载完毕)");

    // 3. 软件复位
    max30102_write_reg(dev, MAX30102_MODE_CONFIG, 0x40); 
    vTaskDelay(pdMS_TO_TICKS(100));

    // 4. 配置传感器参数
    max30102_write_reg(dev, MAX30102_FIFO_CONFIG, 0x1F); 
    max30102_write_reg(dev, MAX30102_MODE_CONFIG, 0x03);
    max30102_write_reg(dev, MAX30102_SPO2_CONFIG, 0x2F);
    max30102_write_reg(dev, MAX30102_LED1_PA, 0x24); 
    max30102_write_reg(dev, MAX30102_LED2_PA, 0x24); 

    max30102_clear_fifo(dev);
    return ESP_OK;
}

/**
 * @brief 清空 MAX30102 的 FIFO 缓冲区指针
 * 
 * 该函数通过将 FIFO 写入指针、溢出计数器和 FIFO 读取指针重置为 0，
 * 来清空 MAX30102 传感器的 FIFO 缓冲区。这在初始化传感器或需要
 * 重新开始数据采集时非常有用。
 * 
 * @param dev I2C 设备句柄
 * @return esp_err_t 操作结果
 *         - ESP_OK: 成功清空 FIFO
 *         - 其他错误码: 操作失败
 */
esp_err_t max30102_clear_fifo(i2c_master_dev_handle_t dev) {
    esp_err_t ret = max30102_write_reg(dev, MAX30102_FIFO_WR_PTR, 0x00);
    ret |= max30102_write_reg(dev, MAX30102_OVF_COUNTER, 0x00);
    ret |= max30102_write_reg(dev, MAX30102_FIFO_RD_PTR, 0x00);
    return ret;
}

/**
 * @brief 从 MAX30102 FIFO 中读取最新的红色和红外光原始数据
 * 
 * 该函数从 MAX30102 传感器的 FIFO 缓冲区中读取 6 个字节的数据，
 * 其中前 3 个字节是红色光数据，后 3 个字节是红外光数据。
 * 数据以 18 位格式存储，需要通过位操作进行解析。
 * 
 * @param dev I2C 设备句柄
 * @param red_data 用于存储红色光数据的指针
 * @param ir_data 用于存储红外光数据的指针
 * @return esp_err_t 操作结果
 *         - ESP_OK: 成功读取数据
 *         - 其他错误码: 读取失败
 */
esp_err_t max30102_read_fifo_data(i2c_master_dev_handle_t dev, uint32_t *red_data, uint32_t *ir_data) {
    uint8_t buffer[6]; 
    esp_err_t ret = max30102_read_reg(dev, MAX30102_FIFO_DATA, buffer, 6);
    if (ret != ESP_OK) return ret;

    *red_data = ((buffer[0] << 16) | (buffer[1] << 8) | buffer[2]) & 0x03FFFF;
    *ir_data  = ((buffer[3] << 16) | (buffer[4] << 8) | buffer[5]) & 0x03FFFF;
    return ESP_OK;
}

/**
 * @brief 读取 MAX30102 芯片内部温度
 * 
 * 该函数读取 MAX30102 传感器的内部温度，温度数据分为整数部分和小数部分，
 * 其中小数部分的分辨率为 0.0625°C。
 * 
 * @param dev I2C 设备句柄
 * @param temp_c 用于存储温度值的指针（摄氏度）
 * @return esp_err_t 操作结果
 *         - ESP_OK: 成功读取温度
 *         - 其他错误码: 读取失败
 */
esp_err_t max30102_read_temperature(i2c_master_dev_handle_t dev, float *temp_c) {
    uint8_t int_part = 0, frac_part = 0;
    max30102_write_reg(dev, MAX30102_DIE_TEMP_CONFIG, 0x01);
    vTaskDelay(pdMS_TO_TICKS(35)); 
    max30102_read_reg(dev, MAX30102_DIE_TEMP_INT, &int_part, 1);
    max30102_read_reg(dev, MAX30102_DIE_TEMP_FRAC, &frac_part, 1);

    int8_t signed_int = (int8_t)int_part; 
    *temp_c = (float)signed_int + ((float)frac_part * 0.0625f);
    return ESP_OK;
}