#include "driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"

#define TAG "driver"

// 触摸屏 I2C 与控制引脚配置
#define TOUCH_I2C_PORT I2C_NUM_0
#define TOUCH_SDA_PIN  GPIO_NUM_21  
#define TOUCH_SCL_PIN  GPIO_NUM_22  
#define TOUCH_RST_PIN  GPIO_NUM_16
#define TOUCH_INT_PIN  GPIO_NUM_17

// 全局触摸屏句柄
cst816s_handle_t g_touch_handle = NULL;
// MAX30102 I2C句柄
i2c_master_dev_handle_t max30102_handle = NULL;
// I2C互斥锁
SemaphoreHandle_t g_i2c_mutex = NULL;

/**
 * @brief 初始化所有驱动
 * 
 * 该函数负责初始化系统的所有驱动，包括：
 * 1. 创建I2C互斥锁
 * 2. 初始化触摸驱动
 * 3. 初始化MPU6500驱动
 * 4. 初始化MAX30102驱动
 * 5. 初始化蓝牙驱动
 * 
 * @return esp_err_t - 操作结果，成功返回ESP_OK
 */
esp_err_t driver_init(void)
{
    esp_err_t ret;
    
    // 创建I2C互斥锁
    g_i2c_mutex = xSemaphoreCreateMutex();
    if (g_i2c_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create I2C mutex");
        return ESP_FAIL;
    }
    
    // 初始化触摸驱动
    ret = touch_driver_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize touch driver");
        return ret;
    }
    
    // 初始化MPU6500驱动
    ret = mpu6500_driver_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize MPU6500 driver");
        return ret;
    }
    
    // 初始化MAX30102驱动
    ret = max30102_driver_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize MAX30102 driver");
        return ret;
    }
    
    // 初始化蓝牙驱动
    ret = ble_driver_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BLE driver");
        return ret;
    }
    
    return ESP_OK;
}

/**
 * @brief 初始化触摸驱动
 * 
 * 该函数负责初始化触摸屏幕驱动，包括：
 * 1. 硬件唤醒复位
 * 2. 配置触摸驱动参数
 * 3. 初始化触摸控制器
 * 
 * @return esp_err_t - 操作结果，成功返回ESP_OK
 */
esp_err_t touch_driver_init(void)
{
    esp_err_t ret;
    
    // 硬件唤醒复位
    ESP_LOGI(TAG, "Resetting touch screen...");
    gpio_reset_pin(TOUCH_RST_PIN);
    gpio_set_direction(TOUCH_RST_PIN, GPIO_MODE_OUTPUT);
    gpio_reset_pin(TOUCH_INT_PIN);
    gpio_set_direction(TOUCH_INT_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(TOUCH_INT_PIN, GPIO_PULLUP_ONLY); 

    gpio_set_level(TOUCH_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(TOUCH_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(160)); // wait for touch controller startup

    // 初始化触摸驱动
    cst816s_config_t touch_cfg = {
        .i2c_port = TOUCH_I2C_PORT,
        .pin_sda = TOUCH_SDA_PIN,
        .pin_scl = TOUCH_SCL_PIN,
        .pin_int = TOUCH_INT_PIN, 
        .pin_rst = TOUCH_RST_PIN,
        .dev_addr = 0x15, 
        .scl_speed_hz = 400000,
        .x_max = 240,
        .y_max = 240
    };
    ret = cst816s_new(&touch_cfg, &g_touch_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize touch screen: %s", esp_err_to_name(ret));
        return ret;
    }
    esp_log_level_set("i2c.master", ESP_LOG_NONE);
    
    return ESP_OK;
}

/**
 * @brief 初始化MPU6500驱动
 * 
 * 该函数负责初始化MPU6500传感器驱动。
 * 
 * @return esp_err_t - 操作结果，成功返回ESP_OK
 */
esp_err_t mpu6500_driver_init(void)
{
    if (mpu6500_init() != ESP_OK) {
        ESP_LOGW(TAG, "If connecting only MPU6500 VCC/GND already causes resets, check power, shorts, and confirm the sensor is on 3.3V.");
        ESP_LOGE(TAG, "MPU6500 初始化失败！请检查接线！");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

/**
 * @brief 初始化MAX30102驱动
 * 
 * 该函数负责初始化MAX30102心率血氧传感器驱动，包括：
 * 1. 获取共享I2C总线
 * 2. 初始化MAX30102传感器
 * 
 * @return esp_err_t - 操作结果，成功返回ESP_OK
 */
esp_err_t max30102_driver_init(void)
{
    esp_err_t ret;
    
    i2c_master_bus_handle_t shared_bus = cst816s_get_i2c_bus(g_touch_handle);
    ESP_LOGW(TAG, ">>> 准备检查 shared_bus...");

    if (shared_bus != NULL) {
        ESP_LOGW(TAG, ">>> shared_bus 不为空，准备进入 init...");
        
        ret = max30102_init(shared_bus, &max30102_handle);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "✅ MAX30102 initialized successfully");
        } else {
            ESP_LOGE(TAG, "❌ MAX30102 init failed with error: %s", esp_err_to_name(ret));
            return ret;
        }
    } else {
        ESP_LOGE(TAG, "❌ Shared bus is NULL, cannot initialize MAX30102");
        return ESP_FAIL;
    }
    
    ESP_LOGW(TAG, ">>> MAX30102 初始化流程走完！");
    return ESP_OK;
}

/**
 * @brief 初始化蓝牙驱动
 * 
 * 该函数负责初始化蓝牙驱动，包括：
 * 1. 初始化NVS
 * 2. 启动蓝牙广播
 * 
 * @return esp_err_t - 操作结果，成功返回ESP_OK
 */
esp_err_t ble_driver_init(void)
{
    esp_err_t ret;
    
    // 初始化NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 启动蓝牙广播
    ble_beacon_init();
    
    return ESP_OK;
}