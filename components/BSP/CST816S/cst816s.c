#include "cst816s.h"

#include <stdlib.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CST816S_REG_TOUCH_NUM        0x02
#define CST816S_REG_XPOS_H           0x03
#define CST816S_REG_CHIP_ID          0xA7
#define CST816S_INIT_RETRY_COUNT     8
#define CST816S_INIT_RETRY_DELAY_MS  80
#define CST816S_IO_RETRY_COUNT       2
#define CST816S_PROBE_TIMEOUT_MS     10
#define CST816S_XFER_TIMEOUT_MS      5
#define CST816S_RECOVERY_DELAY_MS    200
#define CST816S_FAILURE_THRESHOLD    3
#define CST816S_WARN_INTERVAL_US     8000000LL
#define CST816S_RESET_LOW_MS         10
#define CST816S_RESET_HIGH_MS        120

struct cst816s_device_t {
    cst816s_config_t config;
    i2c_master_bus_handle_t i2c_bus;
    i2c_master_dev_handle_t i2c_dev;
    bool online;
    uint8_t chip_id;
    uint32_t consecutive_failures;
    int64_t next_recovery_us;
    int64_t last_warn_us;
};

static const char *TAG = "cst816s";

static esp_err_t cst816s_read_regs(cst816s_handle_t touch, uint8_t reg_addr, uint8_t *data, size_t len);
static esp_err_t cst816s_probe_and_configure(cst816s_handle_t touch, bool log_success);
static esp_err_t cst816s_probe_device(cst816s_handle_t touch);
static void cst816s_mark_offline(cst816s_handle_t touch, const char *stage, esp_err_t err);
static void cst816s_hard_reset(cst816s_handle_t touch);
static esp_err_t cst816s_create_bus(cst816s_handle_t touch);
static void cst816s_destroy_bus(cst816s_handle_t touch);
static esp_err_t cst816s_recreate_bus(cst816s_handle_t touch);
static esp_err_t cst816s_reset_bus(cst816s_handle_t touch);
static void cst816s_note_runtime_failure(cst816s_handle_t touch, const char *stage, esp_err_t err);

/**
 * @brief 创建并初始化 CST816S 触摸设备
 * 
 * 该函数负责创建 CST816S 触摸设备实例，配置 GPIO 引脚，
 * 创建 I2C 总线，尝试初始化触摸芯片，并返回设备句柄。
 * 
 * @param config 触摸设备配置信息，包含 I2C 端口、引脚定义、设备地址等
 * @param ret_touch 用于返回创建的设备句柄
 * @return esp_err_t 操作结果
 *         - ESP_OK: 成功创建并初始化设备
 *         - ESP_ERR_INVALID_ARG: 参数无效
 *         - ESP_ERR_NO_MEM: 内存分配失败
 *         - 其他错误码: I2C 总线创建失败
 */
esp_err_t cst816s_new(const cst816s_config_t *config, cst816s_handle_t *ret_touch)
{
    esp_err_t ret;
    struct cst816s_device_t *touch;

    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is NULL");
    ESP_RETURN_ON_FALSE(ret_touch != NULL, ESP_ERR_INVALID_ARG, TAG, "ret_touch is NULL");

    *ret_touch = NULL;

    touch = calloc(1, sizeof(struct cst816s_device_t));
    ESP_RETURN_ON_FALSE(touch != NULL, ESP_ERR_NO_MEM, TAG, "no mem for cst816s handle");

    touch->config = *config;

    if ((int)touch->config.pin_rst >= 0) {
        gpio_reset_pin(touch->config.pin_rst);
        gpio_set_direction(touch->config.pin_rst, GPIO_MODE_OUTPUT);
    }

    ret = cst816s_create_bus(touch);
    if (ret != ESP_OK) {
        free(touch);
        return ret;
    }

    for (int attempt = 1; attempt <= CST816S_INIT_RETRY_COUNT; attempt++) {
        cst816s_reset_bus(touch);
        cst816s_hard_reset(touch);
        ret = cst816s_probe_and_configure(touch, attempt == CST816S_INIT_RETRY_COUNT);
        if (ret == ESP_OK) {
            *ret_touch = touch;
            ESP_LOGI(TAG, "touch initialized, addr=0x%02X, chip_id=0x%02X", touch->config.dev_addr, touch->chip_id);
            return ESP_OK;
        }

        ESP_LOGW(TAG,
                 "touch init attempt %d/%d failed: %s",
                 attempt,
                 CST816S_INIT_RETRY_COUNT,
                 esp_err_to_name(ret));

        if (attempt < CST816S_INIT_RETRY_COUNT) {
            vTaskDelay(pdMS_TO_TICKS(CST816S_INIT_RETRY_DELAY_MS));
        }
    }

    touch->online = false;
    touch->chip_id = 0;
    touch->consecutive_failures = 0;
    touch->next_recovery_us = 0;
    *ret_touch = touch;

    ESP_LOGW(TAG,
             "touch did not respond during boot (%s), continuing in offline recovery mode",
             esp_err_to_name(ret));
    return ESP_OK;
}

esp_err_t cst816s_read_point(cst816s_handle_t touch, cst816s_point_t *point)
{
    esp_err_t ret;
    uint8_t touch_num = 0;
    uint8_t coord_raw[4] = {0};

    ESP_RETURN_ON_FALSE(touch != NULL, ESP_ERR_INVALID_ARG, TAG, "touch is NULL");
    ESP_RETURN_ON_FALSE(point != NULL, ESP_ERR_INVALID_ARG, TAG, "point is NULL");

    point->pressed = false;
    point->x = 0;
    point->y = 0;

    if (!touch->online) {
        if (esp_timer_get_time() < touch->next_recovery_us) {
            return ESP_OK;
        }

        if (cst816s_reset_bus(touch) != ESP_OK) {
            touch->next_recovery_us = esp_timer_get_time() + (CST816S_RECOVERY_DELAY_MS * 1000LL);
            return ESP_OK;
        }

        if (cst816s_probe_and_configure(touch, false) != ESP_OK) {
            return ESP_OK;
        }
    }

    ret = cst816s_read_regs(touch, CST816S_REG_TOUCH_NUM, &touch_num, 1);
    if (ret != ESP_OK) {
        cst816s_note_runtime_failure(touch, "read touch count", ret);
        return ESP_OK;
    }

    touch->consecutive_failures = 0;

    if ((touch_num & 0x0F) == 0) {
        return ESP_OK;
    }

    ret = cst816s_read_regs(touch, CST816S_REG_XPOS_H, coord_raw, sizeof(coord_raw));
    if (ret != ESP_OK) {
        cst816s_note_runtime_failure(touch, "read touch coordinates", ret);
        return ESP_OK;
    }

    touch->consecutive_failures = 0;
    point->x = (uint16_t)(((coord_raw[0] & 0x0F) << 8) | coord_raw[1]);
    point->y = (uint16_t)(((coord_raw[2] & 0x0F) << 8) | coord_raw[3]);
    point->pressed = true;

    if (point->x >= touch->config.x_max) {
        point->x = touch->config.x_max - 1;
    }
    if (point->y >= touch->config.y_max) {
        point->y = touch->config.y_max - 1;
    }

    return ESP_OK;
}

static esp_err_t cst816s_read_regs(cst816s_handle_t touch, uint8_t reg_addr, uint8_t *data, size_t len)
{
    esp_err_t ret = ESP_FAIL;

    for (int attempt = 0; attempt < CST816S_IO_RETRY_COUNT; attempt++) {
        ret = i2c_master_transmit_receive(touch->i2c_dev,
                                          &reg_addr,
                                          1,
                                          data,
                                          len,
                                          CST816S_XFER_TIMEOUT_MS);
        if (ret == ESP_OK) {
            return ESP_OK;
        }

        if (ret == ESP_ERR_TIMEOUT) {
            cst816s_reset_bus(touch);
        }
    }

    return ret;
}

static esp_err_t cst816s_probe_and_configure(cst816s_handle_t touch, bool log_success)
{
    uint8_t chip_id = 0;
    esp_err_t ret;

    ret = cst816s_probe_device(touch);
    if (ret != ESP_OK) {
        cst816s_mark_offline(touch, "probe device", ret);
        return ret;
    }

    ret = cst816s_read_regs(touch, CST816S_REG_CHIP_ID, &chip_id, 1);
    if (ret != ESP_OK || chip_id == 0x00) {
        cst816s_mark_offline(touch, "probe chip id", ret == ESP_OK ? ESP_ERR_NOT_FOUND : ret);
        return ret == ESP_OK ? ESP_ERR_NOT_FOUND : ret;
    }

    touch->online = true;
    touch->chip_id = chip_id;
    touch->consecutive_failures = 0;
    touch->next_recovery_us = 0;

    if (log_success) {
        ESP_LOGI(TAG, "touch probe OK, chip_id=0x%02X", chip_id);
    }

    return ESP_OK;
}

static esp_err_t cst816s_probe_device(cst816s_handle_t touch)
{
    if (touch == NULL || touch->i2c_bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return i2c_master_probe(touch->i2c_bus, touch->config.dev_addr, CST816S_PROBE_TIMEOUT_MS);
}

static void cst816s_mark_offline(cst816s_handle_t touch, const char *stage, esp_err_t err)
{
    const int64_t now = esp_timer_get_time();

    touch->online = false;
    touch->consecutive_failures++;
    touch->next_recovery_us = now + (CST816S_RECOVERY_DELAY_MS * 1000LL);

    if ((now - touch->last_warn_us) >= CST816S_WARN_INTERVAL_US) {
        ESP_LOGW(TAG,
                 "%s failed (%s), backing off touch reads for %d ms",
                 stage,
                 esp_err_to_name(err),
                 CST816S_RECOVERY_DELAY_MS);
        touch->last_warn_us = now;
    }
}

static void cst816s_note_runtime_failure(cst816s_handle_t touch, const char *stage, esp_err_t err)
{
    touch->consecutive_failures++;

    if (touch->consecutive_failures < CST816S_FAILURE_THRESHOLD) {
        return;
    }

    cst816s_mark_offline(touch, stage, err);
}

static void cst816s_hard_reset(cst816s_handle_t touch)
{
    if ((int)touch->config.pin_rst < 0) {
        return;
    }

    gpio_set_level(touch->config.pin_rst, 0);
    vTaskDelay(pdMS_TO_TICKS(CST816S_RESET_LOW_MS));
    gpio_set_level(touch->config.pin_rst, 1);
    vTaskDelay(pdMS_TO_TICKS(CST816S_RESET_HIGH_MS));
}

static esp_err_t cst816s_create_bus(cst816s_handle_t touch)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = touch->config.i2c_port,
        .sda_io_num = touch->config.pin_sda,
        .scl_io_num = touch->config.pin_scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags.enable_internal_pullup = 1,
    };

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = touch->config.dev_addr,
        .scl_speed_hz = touch->config.scl_speed_hz,
        .scl_wait_us = 0,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_config, &touch->i2c_bus);
    if (ret != ESP_OK) {
        touch->i2c_bus = NULL;
        touch->i2c_dev = NULL;
        return ret;
    }

    ret = i2c_master_bus_add_device(touch->i2c_bus, &dev_config, &touch->i2c_dev);
    if (ret != ESP_OK) {
        i2c_del_master_bus(touch->i2c_bus);
        touch->i2c_bus = NULL;
        touch->i2c_dev = NULL;
        return ret;
    }

    return ESP_OK;
}

static void cst816s_destroy_bus(cst816s_handle_t touch)
{
    if (touch == NULL) {
        return;
    }

    if (touch->i2c_bus != NULL) {
        (void)i2c_master_bus_reset(touch->i2c_bus);
    }

    if (touch->i2c_dev != NULL) {
        (void)i2c_master_bus_rm_device(touch->i2c_dev);
        touch->i2c_dev = NULL;
    }

    if (touch->i2c_bus != NULL) {
        (void)i2c_del_master_bus(touch->i2c_bus);
        touch->i2c_bus = NULL;
    }
}

static esp_err_t cst816s_recreate_bus(cst816s_handle_t touch)
{
    cst816s_destroy_bus(touch);
    return cst816s_create_bus(touch);
}

static esp_err_t cst816s_reset_bus(cst816s_handle_t touch)
{
    if (touch == NULL || touch->i2c_bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = i2c_master_bus_reset(touch->i2c_bus);
    if (ret == ESP_OK) {
        return ESP_OK;
    }

    return cst816s_recreate_bus(touch);
}

/**
 * @brief 从 CST816S 设备句柄中获取 I2C 总线句柄
 * 
 * 该函数用于获取 CST816S 设备内部使用的 I2C 总线句柄，
 * 以便其他设备（如 MAX30102）可以共享同一 I2C 总线，
 * 避免重复创建总线资源。
 * 
 * @param handle CST816S 设备句柄
 * @return i2c_master_bus_handle_t I2C 总线句柄，如果 handle 为 NULL 则返回 NULL
 */
i2c_master_bus_handle_t cst816s_get_i2c_bus(cst816s_handle_t handle) {
    if (handle != NULL) {
        return handle->i2c_bus;
    }
    return NULL;
}
