#ifndef BLE_BEACON_H
#define BLE_BEACON_H

#include <stdint.h>

// 暴露给外部的初始化函数：一键启动蓝牙广播
void ble_beacon_init(void);

void ble_beacon_start(void);
void ble_beacon_stop(void);

void ble_send_health_data(int32_t hr, float spo2);

#endif // BLE_BEACON_H