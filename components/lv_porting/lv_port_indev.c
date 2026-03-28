#include "lv_port_indev.h"
#include "lvgl.h"
#include "cst816s.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

extern cst816s_handle_t g_touch_handle;
extern SemaphoreHandle_t g_i2c_mutex;
extern void app_queue_swipe(lv_dir_t dir);

static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data);

void lv_port_indev_init(void)
{
    static lv_indev_drv_t indev_drv;

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;

    lv_indev_drv_register(&indev_drv);
}

static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;
    static lv_coord_t start_x = 0;
    static lv_coord_t start_y = 0;
    static cst816s_point_t cached_point = {0};
    static bool touch_active = false;
    static bool swipe_sent = false;
    static bool suppress_until_release = false;
    static int64_t last_press_us = 0;
    static int64_t last_sample_us = 0;
    const int64_t sample_interval_us = 20000;
    const int64_t release_guard_us = 140000;
    const lv_coord_t swipe_threshold = 32;
    const lv_coord_t axis_margin = 10;
    const int64_t now_us = esp_timer_get_time();

    // ❌ 删掉了这里错误在顶部的拿锁代码！

    LV_UNUSED(indev_drv);

    if (g_touch_handle == NULL) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    if ((now_us - last_sample_us) >= sample_interval_us) {
        cst816s_point_t point = {0};
        
        // ==========================================
        // 🌟 核心修复：只在这里拿锁、读数据、还锁！
        // ==========================================
        esp_err_t err = ESP_FAIL;
        
        if (g_i2c_mutex != NULL) {
            xSemaphoreTake(g_i2c_mutex, portMAX_DELAY); // 1. 拿锁
        }
        
        err = cst816s_read_point(g_touch_handle, &point); // 2. 读I2C
        
        if (g_i2c_mutex != NULL) {
            xSemaphoreGive(g_i2c_mutex); // 3. 立刻还锁！！！
        }
        // ==========================================

        if (err == ESP_OK) {
            cached_point = point;
        }
        last_sample_us = now_us;
    }

    if (suppress_until_release) {
        data->state = LV_INDEV_STATE_REL;
        data->point.x = last_x;
        data->point.y = last_y;

        if (!cached_point.pressed && ((now_us - last_press_us) >= release_guard_us)) {
            suppress_until_release = false;
            touch_active = false;
            swipe_sent = false;
        }
        return;
    }

    if (cached_point.pressed) {
        data->state = LV_INDEV_STATE_PR;
        last_x = cached_point.x;
        last_y = cached_point.y;
        last_press_us = now_us;

        if (!touch_active) {
            touch_active = true;
            swipe_sent = false;
            start_x = cached_point.x;
            start_y = cached_point.y;
        } else if (!swipe_sent) {
            lv_coord_t dx = cached_point.x - start_x;
            lv_coord_t dy = cached_point.y - start_y;

            if ((LV_ABS(dx) >= swipe_threshold) && (LV_ABS(dx) > (LV_ABS(dy) + axis_margin))) {
                app_queue_swipe(dx < 0 ? LV_DIR_LEFT : LV_DIR_RIGHT);
                swipe_sent = true;
                suppress_until_release = true;
                data->state = LV_INDEV_STATE_REL;
            }
        }
    } else if (touch_active && ((now_us - last_press_us) < release_guard_us)) {
        data->state = LV_INDEV_STATE_PR;
    } else {
        if (touch_active && !swipe_sent) {
            lv_coord_t dx = last_x - start_x;
            lv_coord_t dy = last_y - start_y;

            if ((LV_ABS(dx) >= swipe_threshold) && (LV_ABS(dx) > (LV_ABS(dy) + axis_margin))) {
                app_queue_swipe(dx < 0 ? LV_DIR_LEFT : LV_DIR_RIGHT);
            }
        }

        data->state = LV_INDEV_STATE_REL;
        touch_active = false;
        swipe_sent = false;
    }

    data->point.x = last_x;
    data->point.y = last_y;
}