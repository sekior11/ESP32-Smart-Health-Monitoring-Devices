#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"

#include "driver.h"
#include "business.h"
#include "ui.h"

static const char *TAG = "app_main";

/**
 * @brief 将复位原因转换为字符串
 * 
 * 该函数负责将ESP32的复位原因枚举值转换为对应的字符串描述。
 * 
 * @param reason - 复位原因枚举值
 * @return const char* - 复位原因的字符串描述
 */
static const char *reset_reason_to_string(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_UNKNOWN:   return "unknown";
        case ESP_RST_POWERON:   return "power-on";
        case ESP_RST_EXT:       return "external pin";
        case ESP_RST_SW:        return "software";
        case ESP_RST_PANIC:     return "panic";
        case ESP_RST_INT_WDT:   return "interrupt watchdog";
        case ESP_RST_TASK_WDT:  return "task watchdog";
        case ESP_RST_WDT:       return "other watchdog";
        case ESP_RST_DEEPSLEEP: return "deep sleep";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_SDIO:      return "sdio";
        default:                return "unhandled";
    }
}

/**
 * @brief 记录启动诊断信息
 * 
 * 该函数负责记录系统启动时的诊断信息，包括复位原因和相关警告。
 */
static void log_boot_diagnostics(void)
{
    const esp_reset_reason_t reason = esp_reset_reason();

    ESP_LOGI(TAG, "Reset reason: %s (%d)", reset_reason_to_string(reason), reason);

    if (reason == ESP_RST_BROWNOUT) {
        ESP_LOGW(TAG, "Brownout detected. If MPU6500 VCC/GND causes reboot, inspect wiring polarity, short circuits, and 3.3V stability before changing more code.");
    }
}

/**
 * @brief 应用程序主函数
 * 
 * 该函数是应用程序的入口点，负责：
 * 1. 记录启动诊断信息
 * 2. 初始化驱动层
 * 3. 初始化业务层
 * 4. 初始化UI层
 * 5. 创建健康监测任务
 */
void app_main(void)
{
    esp_err_t ret;
    
    log_boot_diagnostics();

    // 初始化驱动层
    ret = driver_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize drivers: %s", esp_err_to_name(ret));
        return;
    }

    // 初始化业务层
    ret = business_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize business logic: %s", esp_err_to_name(ret));
        return;
    }

    // 初始化UI层
    ret = ui_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize UI: %s", esp_err_to_name(ret));
        return;
    }

    // 创建健康监测任务
    xTaskCreate(max30102_blood_task, "blood_task", 1024 * 16, NULL, 3, NULL);

    ESP_LOGI(TAG, "LVGL application started");
}