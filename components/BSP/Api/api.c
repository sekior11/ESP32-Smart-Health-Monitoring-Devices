#include "api.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include <string.h>
#include "cJSON.h"
#include "esp_crt_bundle.h"

static const char *TAG = "MY_CLOUD";
void parse_json_data(const char *json_string);

extern char g_weather_city[32];
extern char g_weather_condition[32];
extern int g_weather_temp;
extern int g_weather_humidity;

// 在文件上方定义一个大一点的缓冲区大小
#define MAX_HTTP_RECV_BUFFER 1024
static char *s_recv_buffer = NULL; // 用于拼接数据的全局指针
static int s_recv_len = 0;         // 当前接收到的数据长度

/**
 * @brief HTTP事件回调函数
 * 
 * 该函数负责处理HTTP客户端的各种事件，包括：
 * 1. 错误处理
 * 2. 连接处理
 * 3. 数据接收处理
 * 4. 请求完成处理
 * 5. 断开连接处理
 * 
 * @param evt - HTTP客户端事件
 * @return esp_err_t - 操作结果，成功返回ESP_OK
 */
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE("MY_CLOUD", "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI("MY_CLOUD", "HTTP_EVENT_ON_CONNECTED");
            // 连上服务器时，分配一块干净的内存用来装数据
            if (s_recv_buffer == NULL) {
                s_recv_buffer = calloc(1, MAX_HTTP_RECV_BUFFER);
            }
            s_recv_len = 0;
            break;
        case HTTP_EVENT_HEADER_SENT:
            break;
        case HTTP_EVENT_ON_HEADER:
            break;
        case HTTP_EVENT_ON_DATA:
            if (s_recv_buffer != NULL) {
                // 防止数据超长溢出
                if (s_recv_len + evt->data_len < MAX_HTTP_RECV_BUFFER) {
                    memcpy(s_recv_buffer + s_recv_len, evt->data, evt->data_len);
                    s_recv_len += evt->data_len;
                } else {
                    ESP_LOGE("MY_CLOUD", "接收缓冲区溢出！数据太长了");
                }
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI("MY_CLOUD", "HTTP_EVENT_ON_FINISH");
            if (s_recv_buffer != NULL) {
                ESP_LOGI("MY_CLOUD", "接收到的完整数据:\n%s", s_recv_buffer);
                

                parse_json_data(s_recv_buffer);
                
                // 释放内存
                free(s_recv_buffer);
                s_recv_buffer = NULL;
            }
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI("MY_CLOUD", "HTTP_EVENT_DISCONNECTED");
            if (s_recv_buffer != NULL) {
                free(s_recv_buffer);
                s_recv_buffer = NULL;
            }
            break;
        case HTTP_EVENT_REDIRECT:
            break;
    }
    return ESP_OK;
}
/**
 * @brief API请求函数
 * 
 * 该函数负责发起HTTP GET请求获取天气数据，包括：
 * 1. 配置HTTP客户端
 * 2. 初始化客户端
 * 3. 执行HTTP请求
 * 4. 处理请求结果
 * 5. 清理资源
 */
void my_cloud_fetch_data(void)
{

    esp_http_client_config_t config = {
        .url = "https://restapi.amap.com/v3/weather/weatherInfo?key=a1ec6a08452b1c14863cc22a7c4326ff&city=410700&extensions=base",
        .event_handler = _http_event_handler,
        .timeout_ms = 5000,
		.crt_bundle_attach = esp_crt_bundle_attach,
    };

    // 初始化客户端
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "HTTP 客户端初始化失败");
        return;
    }

    ESP_LOGI(TAG, "正在发起 HTTP GET 请求...");
    
    // 执行请求 (这里会阻塞，直到收到响应或超时)
    esp_err_t err = esp_http_client_perform(client);

    // 检查请求结果
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP GET 请求成功, 状态码: %d, 内容长度: %lld",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP GET 请求失败: %s", esp_err_to_name(err));
    }

    // 务必清理并释放资源，防止内存泄漏
    esp_http_client_cleanup(client);
}

/**
 * @brief JSON解析函数
 * 
 * 该函数负责解析HTTP响应中的JSON数据，提取天气信息并更新全局变量，包括：
 * 1. 解析JSON根对象
 * 2. 检查状态码
 * 3. 提取天气数据
 * 4. 更新全局变量
 * 5. 清理JSON对象
 * 
 * @param json_string - JSON字符串
 */
void parse_json_data(const char *json_string)
{
    cJSON *root = cJSON_Parse(json_string);
    if (root == NULL) return;

    cJSON *status = cJSON_GetObjectItem(root, "status");
    if (status == NULL || status->valuestring == NULL || strcmp(status->valuestring, "1") != 0) {
        cJSON_Delete(root);
        return;
    }

    cJSON *lives_array = cJSON_GetObjectItem(root, "lives");
    if (lives_array != NULL && cJSON_IsArray(lives_array)) {
        cJSON *live_data = cJSON_GetArrayItem(lives_array, 0);
        if (live_data != NULL) {
            cJSON *city = cJSON_GetObjectItem(live_data, "city");
            cJSON *weather = cJSON_GetObjectItem(live_data, "weather");
            cJSON *temperature = cJSON_GetObjectItem(live_data, "temperature");
            cJSON *humidity = cJSON_GetObjectItem(live_data, "humidity");

            // 🌟 3. 最关键的一步：把解析出来的字符串，拷贝到全局变量里！
            if (city && weather && temperature) {
                // 使用 strncpy 拷贝字符串，防止溢出
                strncpy(g_weather_city, city->valuestring, sizeof(g_weather_city) - 1);
                strncpy(g_weather_condition, weather->valuestring, sizeof(g_weather_condition) - 1);
                
                // 温度和湿度在 JSON 里是字符串格式，用 atoi 转换成整数
                g_weather_temp = atoi(temperature->valuestring);
                
                if (humidity) {
                    g_weather_humidity = atoi(humidity->valuestring);
                }
                
                ESP_LOGI("WEATHER", "天气数据已更新到全局变量！准备显示...");
            }
        }
    }
    cJSON_Delete(root);
}