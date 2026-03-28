#include "ble_beacon.h"
#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include <string.h>
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "BLE_BEACON";
static uint8_t own_addr_type;

static uint8_t own_addr_type;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE; // 保存当前的连接句柄
static uint16_t s_sensor_char_handle;
// 向前声明
static void ble_app_advertise(void);


// GAP 事件回调：处理连接、断开等底层事件
static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                ESP_LOGI(TAG, "🎉 手机已成功连接到 ESP32！");
                s_conn_handle = event->connect.conn_handle; // 记录连接句柄
            } else {
                ESP_LOGE(TAG, "❌ 连接失败，状态码: %d", event->connect.status);
                ble_app_advertise();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "👋 手机已断开连接");
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE; // 清除连接句柄
            // 注意：这里暂时注释掉自动重新广播，改为由按键控制
            // ble_app_advertise(); 
            break;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            ESP_LOGI(TAG, "广播停止，准备重启...");
            ble_app_advertise(); 
            break;
    }
    return 0;
}

// 核心：配置并启动广播
static void ble_app_advertise(void) {
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *device_name;
    int rc;

    // 1. 配置广播包内容
    memset(&fields, 0, sizeof fields);
    device_name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "设置广播数据失败: %d", rc);
        return;
    }

    // 2. 配置广播参数
    memset(&adv_params, 0, sizeof adv_params);
    // 改为 NON (不可连接模式)，作为一个纯粹的单向发射器
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; 
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    // 3. 启动广播
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "启动广播失败: %d", rc);
    } else {
        ESP_LOGI(TAG, "蓝牙灯塔已成功点亮！");
    }
}

// 同步回调：当 NimBLE 主机栈就绪时触发
static void ble_on_sync(void) {
    ble_hs_id_infer_auto(0, &own_addr_type); // 自动推断设备地址
    ble_app_advertise();                     // 开始广播
}

// 供 FreeRTOS 调用的宿主任务
static void ble_host_task(void *param) {
    ESP_LOGI(TAG, "BLE Host Task 启动");
    nimble_port_run(); // 这是一个死循环，专门处理蓝牙协议栈事件
    nimble_port_freertos_deinit();
}

static int sensor_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg) {
    return 0;
}

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0xABCD),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(0x1234),
                .access_cb = sensor_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_sensor_char_handle, 
            },
            { 0 } 
        }
    },
    { 0 } 
};

// ==========================================
// 外部接口实现
// ==========================================
void ble_beacon_init(void) {
    ESP_LOGI(TAG, "初始化 NimBLE 协议栈...");
    nimble_port_init();

    ble_svc_gap_device_name_set("ESP-Health"); // 改个酷一点的名字

    // --- 新增：向底层注册我们的 GATT 服务表 ---
    ble_gatts_count_cfg(gatt_svr_svcs);
    ble_gatts_add_svcs(gatt_svr_svcs);
    // -----------------------------------------

    ble_hs_cfg.sync_cb = ble_on_sync;
    nimble_port_freertos_init(ble_host_task);
}
// ==========================================
// 手动控制接口
// ==========================================
void ble_beacon_start(void) {
    // 如果当前没有在广播，且没有设备连接，则开启广播
    if (!ble_gap_adv_active() && s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGI(TAG, "手动开启蓝牙广播...");
        ble_app_advertise();
    }
}

void ble_beacon_stop(void) {
    int rc;
    // 1. 如果正在广播，停止广播
    if (ble_gap_adv_active()) {
        rc = ble_gap_adv_stop();
        ESP_LOGI(TAG, "停止蓝牙广播, 状态: %d", rc);
    }
    // 2. 如果已经连接了手机，主动踢掉（断开）手机
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        rc = ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        ESP_LOGI(TAG, "主动断开蓝牙连接, 状态: %d", rc);
    }
}



// ==========================================
// 新增：向手机推送数据的接口
// ==========================================
void ble_send_health_data(int32_t hr, float spo2) {
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "HR:%ld,SpO2:%d", hr, (int)spo2);

    // --- 修改这里：增加内存判空保护 ---
    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, strlen(buf));
    if (om == NULL) {
        ESP_LOGE(TAG, "❌ BLE 内存不足，无法分配 mbuf！跳过本次推送");
        return; // 必须 return，否则下面代码会引发系统崩溃卡死！
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, s_sensor_char_handle, om);
    if (rc == 0) {
        ESP_LOGI(TAG, "蓝牙数据已推送: %s", buf);
    } else {
        // 错误码 532 (0x0214) 表示手机端没有开启 Notify 订阅，这是正常的，不用管
        ESP_LOGE(TAG, "蓝牙推送失败，错误码: %d", rc);
    }
}