#ifndef BUSINESS_H
#define BUSINESS_H

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "protocol_examples_common.h"

// 页面类型定义
typedef enum {
    PAGE_HOME = 0,
    PAGE_SETTINGS,
    PAGE_HEALTH,
    PAGE_GAME,
    PAGE_WEATHER,
    PAGE_BLUETOOTH,
} app_page_t;

// 天气数据结构
typedef struct {
    char city[32];
    int temp;
    char condition[32];
    int humidity;
} weather_data_t;

// 供 UI 读取的全局变量
extern int32_t final_hr; 
extern float final_spo2;

// 全局天气数据
extern char g_weather_city[32];
extern char g_weather_condition[32];
extern int g_weather_temp;
extern int g_weather_humidity;

// 初始化业务逻辑
esp_err_t business_init(void);

// 健康监测相关
void max30102_blood_task(void *pvParameters);
void get_mpu6050_data(float *pitch, float *roll);

// 天气相关
void fetch_real_weather_api(weather_data_t *data);
void update_weather_ui(void);

#endif // BUSINESS_H