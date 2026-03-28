#include "business.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver.h"
#include "mpu6500.h"
#include "algorithm.h"
#include "ble_beacon.h"
#include "api.h"

#define TAG "business"

#define SAMPLE_RATE 100//采样率，单位：次/秒
#define UPDATE_RATE 50//更新率，单位：次/秒

// 供 UI 读取的全局变量
int32_t final_hr = 0; 
float final_spo2 = 0.0f;

// 全局天气数据
char g_weather_city[32] = "Waiting...";
char g_weather_condition[32] = "--";
int g_weather_temp = 0;
int g_weather_humidity = 0;

/**
 * @brief 初始化业务逻辑
 * 
 * 该函数负责初始化系统的业务逻辑，包括：
 * 1. 初始化NVS闪存
 * 2. 初始化网络接口和事件循环
 * 3. 连接网络
 * 4. 发起API请求获取数据
 * 
 * @return esp_err_t - 操作结果，成功返回ESP_OK
 */
esp_err_t business_init(void)
{
    esp_err_t ret;
    
    // 初始化网络
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 初始化网络接口和事件循环
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 连接网络
    ESP_LOGI(TAG, "正在连接网络...");
    ESP_ERROR_CHECK(example_connect());
    ESP_LOGI(TAG, "网络连接成功！");

    // 发起 API 请求
    my_cloud_fetch_data();
    
    return ESP_OK;
}

/**
 * @brief 健康监测任务
 * 
 * 该任务负责通过MAX30102传感器监测用户的心率和血氧饱和度，包括：
 * 1. 预填充512个采样点
 * 2. 定期更新传感器数据
 * 3. 执行FFT变换分析数据
 * 4. 计算心率和血氧饱和度
 * 5. 定期打印日志和推送蓝牙数据
 * 
 * @param pvParameters - 任务参数（未使用）
 */
void max30102_blood_task(void *pvParameters) 
{
    float ring_red[FFT_N] = {0};
    float ring_ir[FFT_N] = {0};

    int write_index = 0;


    // 创建两个包含 512 个复数的数组用于 FFT
    struct compx s1_red[FFT_N];
    struct compx s2_ir[FFT_N];
    uint32_t raw_red, raw_ir;

    ESP_LOGI("BLOOD", "正在预填充 512 个采样点 (约 5 秒)...");

    for(int i = 0; i < FFT_N; i++) {
        while(max30102_read_fifo_data(max30102_handle, &raw_red, &raw_ir) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        ring_red[i] = (float)raw_red;
        ring_ir[i]  = (float)raw_ir;
        vTaskDelay(pdMS_TO_TICKS(10)); //预填满，防止有 0 数据
    }

    // 日志打印计数器
    int log_counter = 0; 
    int ble_push_counter = 0; // 专门控制蓝牙推送频率的计数器

    while (1) {
        float dc_red = 0, dc_ir = 0;

        // 1. 每次循环更新 50 个数据点 (UPDATE_RATE)，耗时约 0.5 秒
        for(int i = 0; i < UPDATE_RATE; i++)
        {
            esp_err_t err;
            do {
                // 核心：拿锁 -> 读数据 -> 还锁
                xSemaphoreTake(g_i2c_mutex, portMAX_DELAY);
                err = max30102_read_fifo_data(max30102_handle, &raw_red, &raw_ir);
                xSemaphoreGive(g_i2c_mutex);

                // 如果没读成功，等 5ms 再重试，把 I2C 让给触摸屏
                if (err != ESP_OK) {
                    vTaskDelay(pdMS_TO_TICKS(5));
                }
            } while(err != ESP_OK);

            ring_red[write_index] = (float)raw_red;
            ring_ir[write_index] = (float)raw_ir;
            write_index = (write_index + 1) % FFT_N;
            
            vTaskDelay(pdMS_TO_TICKS(15));
        }
        
        for (int i = 0; i < FFT_N; i++) {
            uint16_t read_index = (write_index + i)%FFT_N;
            s1_red[i].real = ring_red[read_index];
            s2_ir[i].real = ring_ir[read_index];
            
            dc_red += s1_red[i].real;
            dc_ir += s2_ir[i].real;
        }

        // 计算平均值 (DC 基线)
        dc_red /= FFT_N;
        dc_ir /= FFT_N;

        // 2. 消除直流偏置，生成纯交流波形 (AC)，并将其补零为复数
        for (int i = 0; i < FFT_N; i++) {
            s1_red[i].real -= dc_red;
            s1_red[i].imag = 0;
            s2_ir[i].real -= dc_ir;
            s2_ir[i].imag = 0;
        }

        // 3. 执行快速傅里叶变换 (时域转频域)
        FFT(s1_red);
        FFT(s2_ir);

        // 4. 解算复数振幅
        float ac_red = 0, ac_ir = 0;
        for (int i = 0; i < FFT_N; i++) {
            s1_red[i].real = sqrtf(s1_red[i].real * s1_red[i].real + s1_red[i].imag * s1_red[i].imag);
            s2_ir[i].real  = sqrtf(s2_ir[i].real * s2_ir[i].real + s2_ir[i].imag * s2_ir[i].imag); 
        }

        // 5. 提取除了极低频以外的交流总能量
        for (int i = 1; i < FFT_N; i++) {
            ac_red += s1_red[i].real;
            ac_ir  += s2_ir[i].real;
        }

        // 6. 寻找心率波峰 (使用红光数据寻找主频)
        int s1_max_index = find_max_num_index(s1_red, 30);
        
        // 最终结果计算 (后台依然保持每 0.5s 更新一次数据)
        final_hr = (int32_t)(60.0f * ((SAMPLE_RATE * s1_max_index) / (float)FFT_N));

        float R = (ac_ir * dc_red) / (ac_red * dc_ir);
        final_spo2 = -45.060f * R * R + 30.354f * R + 94.845f;

        if(final_spo2 > 100.0f) final_spo2 = 99.99f;
        if(final_spo2 < 0.0f) final_spo2 = 0.0f;

        if (s1_max_index < 2) {
            final_hr = 0;
            final_spo2 = 0;
        }

        // 限制日志打印的条件
        log_counter++;
        // 每次大循环大概是 0.5 秒，当计数器达到 10 次时，就是 5 秒
        if (log_counter >= 10) {
            // 只有当屏幕停留在健康页面时，才打印日志
            // 注意：这里需要在UI层实现current_page的获取
            // if (current_page == PAGE_HEALTH) {
                if (s1_max_index < 2) {
                    ESP_LOGW("BLOOD", "未检测到有效脉搏！");
                } else {
                    ESP_LOGI("BLOOD", "✅ 计算成功！HR: %ld BPM, SpO2: %.2f%%", final_hr, final_spo2);
                }
            // }
            log_counter = 0; // 重置计数器，等待下一个 5 秒
        }
        // 新增：如果测量到有效脉搏，且蓝牙已连接，就把数据发给手机
        ble_push_counter++;
         if (ble_push_counter >= 6) { // 6 次 * 0.5 秒 = 3 秒
            // 如果测量到有效脉搏，就把数据发给手机
            if (s1_max_index >= 2) {
                ble_send_health_data(final_hr, final_spo2);
            }
            ble_push_counter = 0; // 重置蓝牙推送计数器
        }
        
    }
}

/**
 * @brief 获取MPU6050数据
 * 
 * 该函数负责从MPU6050传感器读取姿态数据，包括俯仰角和滚转角。
 * 
 * @param pitch - 指向存储俯仰角的指针
 * @param roll - 指向存储滚转角的指针
 */
void get_mpu6050_data(float *pitch, float *roll)
{
    mpu6500_attitude_t attitude = {0};
    esp_err_t  err;
    err = mpu6500_read_attitude(&attitude);

    if(err == ESP_OK)
    {
        *pitch = attitude.pitch;
        *roll = attitude.roll;
    }
    else
    {
        *pitch = 0.0f;
        *roll = 0.0f;
    }
  
}

/**
 * @brief 获取真实天气API数据
 * 
 * 该函数负责从天气API获取真实的天气数据。
 * 
 * @param data - 指向存储天气数据的结构体指针
 */
void fetch_real_weather_api(weather_data_t *data)
{
    // 这里可以实现真实的天气API调用
}