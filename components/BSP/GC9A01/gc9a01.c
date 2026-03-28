#include "gc9a01.h"

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define GC9A01_CMD_SLPOUT           0x11
#define GC9A01_CMD_INVON            0x21
#define GC9A01_CMD_DISPON           0x29
#define GC9A01_CMD_CASET            0x2A
#define GC9A01_CMD_RASET            0x2B
#define GC9A01_CMD_RAMWR            0x2C
#define GC9A01_CMD_MADCTL           0x36
#define GC9A01_CMD_COLMOD           0x3A

#define GC9A01_SPI_QUEUE_SIZE       2

typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t data_len;
    uint16_t delay_ms;
} gc9a01_init_cmd_t;

typedef enum {
    GC9A01_TRANS_TYPE_CMD = 0,
    GC9A01_TRANS_TYPE_DATA,
    GC9A01_TRANS_TYPE_COLOR,
} gc9a01_trans_type_t;

typedef struct {
    gc9a01_trans_type_t type;
    struct gc9a01_device_t *panel;
    gc9a01_color_trans_done_cb_t done_cb;
    void *user_ctx;
} gc9a01_trans_user_t;

struct gc9a01_device_t {
    gc9a01_config_t config;
    spi_device_handle_t spi_dev;
    spi_transaction_t color_trans;
    gc9a01_trans_user_t cmd_user;
    gc9a01_trans_user_t data_user;
    gc9a01_trans_user_t color_user;
};

static const char *TAG = "gc9a01";

static const gc9a01_init_cmd_t s_gc9a01_init_cmds[] = {
    {0xEF, {0}, 0, 0},
    {0xEB, {0x14}, 1, 0},
    {0xFE, {0}, 0, 0},
    {0xEF, {0}, 0, 0},
    {0xEB, {0x14}, 1, 0},
    {0x84, {0x40}, 1, 0},
    {0x85, {0xFF}, 1, 0},
    {0x86, {0xFF}, 1, 0},
    {0x87, {0xFF}, 1, 0},
    {0x88, {0x0A}, 1, 0},
    {0x89, {0x21}, 1, 0},
    {0x8A, {0x00}, 1, 0},
    {0x8B, {0x80}, 1, 0},
    {0x8C, {0x01}, 1, 0},
    {0x8D, {0x01}, 1, 0},
    {0x8E, {0xFF}, 1, 0},
    {0x8F, {0xFF}, 1, 0},
    {0xB6, {0x00, 0x20}, 2, 0},
    {GC9A01_CMD_MADCTL, {0x08}, 1, 0},
    {GC9A01_CMD_COLMOD, {0x55}, 1, 0},
    {0x90, {0x08, 0x08, 0x08, 0x08}, 4, 0},
    {0xBD, {0x06}, 1, 0},
    {0xBC, {0x00}, 1, 0},
    {0xFF, {0x60, 0x01, 0x04}, 3, 0},
    {0xC3, {0x13}, 1, 0},
    {0xC4, {0x13}, 1, 0},
    {0xC9, {0x22}, 1, 0},
    {0xBE, {0x11}, 1, 0},
    {0xE1, {0x10, 0x0E}, 2, 0},
    {0xDF, {0x21, 0x0C, 0x02}, 3, 0},
    {0xF0, {0x45, 0x09, 0x08, 0x08, 0x26, 0x2A}, 6, 0},
    {0xF1, {0x43, 0x70, 0x72, 0x36, 0x37, 0x6F}, 6, 0},
    {0xF2, {0x45, 0x09, 0x08, 0x08, 0x26, 0x2A}, 6, 0},
    {0xF3, {0x43, 0x70, 0x72, 0x36, 0x37, 0x6F}, 6, 0},
    {0xED, {0x1B, 0x0B}, 2, 0},
    {0xAE, {0x77}, 1, 0},
    {0xCD, {0x63}, 1, 0},
    {0x70, {0x07, 0x07, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03}, 9, 0},
    {0xE8, {0x34}, 1, 0},
    {0x62, {0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70}, 12, 0},
    {0x63, {0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70}, 12, 0},
    {0x64, {0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07}, 7, 0},
    {0x66, {0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00}, 10, 0},
    {0x67, {0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98}, 10, 0},
    {0x74, {0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00}, 7, 0},
    {0x98, {0x3E, 0x07}, 2, 0},
    {GC9A01_CMD_INVON, {0}, 0, 0},
    {GC9A01_CMD_SLPOUT, {0}, 0, 120},
    {GC9A01_CMD_DISPON, {0}, 0, 20},
};

static void gc9a01_spi_pre_cb(spi_transaction_t *trans);
static void gc9a01_spi_post_cb(spi_transaction_t *trans);
static esp_err_t gc9a01_send_cmd(gc9a01_handle_t panel, uint8_t cmd);
static esp_err_t gc9a01_send_data(gc9a01_handle_t panel, const void *data, size_t len);
static esp_err_t gc9a01_set_window(gc9a01_handle_t panel, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);

esp_err_t gc9a01_new_panel(const gc9a01_config_t *config, gc9a01_handle_t *ret_panel)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is NULL");
    ESP_RETURN_ON_FALSE(ret_panel != NULL, ESP_ERR_INVALID_ARG, TAG, "ret_panel is NULL");

    struct gc9a01_device_t *panel = calloc(1, sizeof(struct gc9a01_device_t));
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_NO_MEM, TAG, "no mem for gc9a01 handle");

    panel->config = *config;
    panel->cmd_user.type = GC9A01_TRANS_TYPE_CMD;
    panel->cmd_user.panel = panel;
    panel->data_user.type = GC9A01_TRANS_TYPE_DATA;
    panel->data_user.panel = panel;
    panel->color_user.type = GC9A01_TRANS_TYPE_COLOR;
    panel->color_user.panel = panel;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = panel->config.pin_mosi,
        .miso_io_num = -1,
        .sclk_io_num = panel->config.pin_sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)panel->config.max_transfer_sz,
    };

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = (int)panel->config.pixel_clock_hz,
        .mode = 0,
        .spics_io_num = panel->config.pin_cs,
        .queue_size = GC9A01_SPI_QUEUE_SIZE,
        .flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_RETURN_RESULT,
        .pre_cb = gc9a01_spi_pre_cb,
        .post_cb = gc9a01_spi_post_cb,
    };

    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << panel->config.pin_dc) |
                        (1ULL << panel->config.pin_rst) |
                        (1ULL << panel->config.pin_blk),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_cfg);
    if (ret != ESP_OK) {
        free(panel);
        return ret;
    }

    gpio_set_level(panel->config.pin_dc, 0);
    gpio_set_level(panel->config.pin_blk, 0);

    ret = spi_bus_initialize(panel->config.spi_host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        free(panel);
        return ret;
    }

    ret = spi_bus_add_device(panel->config.spi_host, &dev_cfg, &panel->spi_dev);
    if (ret != ESP_OK) {
        free(panel);
        return ret;
    }

    *ret_panel = panel;
    return ESP_OK;
}

esp_err_t gc9a01_reset(gc9a01_handle_t panel)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is NULL");

    gpio_set_level(panel->config.pin_rst, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(panel->config.pin_rst, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(panel->config.pin_rst, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    return ESP_OK;
}

esp_err_t gc9a01_init(gc9a01_handle_t panel)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is NULL");

    for (size_t i = 0; i < sizeof(s_gc9a01_init_cmds) / sizeof(s_gc9a01_init_cmds[0]); i++) {
        ESP_RETURN_ON_ERROR(gc9a01_send_cmd(panel, s_gc9a01_init_cmds[i].cmd), TAG, "send init cmd failed");
        if (s_gc9a01_init_cmds[i].data_len > 0) {
            ESP_RETURN_ON_ERROR(gc9a01_send_data(panel, s_gc9a01_init_cmds[i].data, s_gc9a01_init_cmds[i].data_len),
                                TAG,
                                "send init data failed");
        }
        if (s_gc9a01_init_cmds[i].delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(s_gc9a01_init_cmds[i].delay_ms));
        }
    }

    return ESP_OK;
}

esp_err_t gc9a01_set_backlight(gc9a01_handle_t panel, bool on)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is NULL");
    return gpio_set_level(panel->config.pin_blk, on ? 1 : 0);
}

esp_err_t gc9a01_draw_bitmap(gc9a01_handle_t panel,
                             uint16_t x1,
                             uint16_t y1,
                             uint16_t x2,
                             uint16_t y2,
                             const void *color_data,
                             size_t color_size)
{
    spi_transaction_t trans = {0};

    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is NULL");
    ESP_RETURN_ON_FALSE(color_data != NULL, ESP_ERR_INVALID_ARG, TAG, "color_data is NULL");
    ESP_RETURN_ON_FALSE(color_size > 0, ESP_ERR_INVALID_ARG, TAG, "color_size is 0");

    ESP_RETURN_ON_ERROR(gc9a01_set_window(panel, x1, y1, x2, y2), TAG, "set window failed");

    trans.length = color_size * 8;
    trans.tx_buffer = color_data;
    trans.user = &panel->color_user;
    panel->color_user.done_cb = NULL;
    panel->color_user.user_ctx = NULL;

    return spi_device_polling_transmit(panel->spi_dev, &trans);
}

esp_err_t gc9a01_draw_bitmap_async(gc9a01_handle_t panel,
                                   uint16_t x1,
                                   uint16_t y1,
                                   uint16_t x2,
                                   uint16_t y2,
                                   const void *color_data,
                                   size_t color_size,
                                   gc9a01_color_trans_done_cb_t done_cb,
                                   void *user_ctx)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is NULL");
    ESP_RETURN_ON_FALSE(color_data != NULL, ESP_ERR_INVALID_ARG, TAG, "color_data is NULL");
    ESP_RETURN_ON_FALSE(color_size > 0, ESP_ERR_INVALID_ARG, TAG, "color_size is 0");

    /*
     * 这里先同步发送列地址、行地址和 RAMWR 命令，
     * 再把整块像素数据作为一个异步 SPI DMA 事务挂到队列里。
     * 这样 LVGL 可以在 DMA 发送当前块数据的同时，继续往另一块缓冲区渲染下一块区域。
     */
    ESP_RETURN_ON_ERROR(gc9a01_set_window(panel, x1, y1, x2, y2), TAG, "set window failed");

    memset(&panel->color_trans, 0, sizeof(panel->color_trans));
    panel->color_user.done_cb = done_cb;
    panel->color_user.user_ctx = user_ctx;
    panel->color_trans.length = color_size * 8;
    panel->color_trans.tx_buffer = color_data;
    panel->color_trans.user = &panel->color_user;

    /*
     * 这里不会额外拷贝像素数据，SPI DMA 直接读取调用者给出的缓冲区。
     * 因此 color_data 必须来自 DMA 可访问内存，例如：
     * heap_caps_malloc(size, MALLOC_CAP_DMA)
     */
    return spi_device_queue_trans(panel->spi_dev, &panel->color_trans, portMAX_DELAY);
}

static esp_err_t gc9a01_send_cmd(gc9a01_handle_t panel, uint8_t cmd)
{
    spi_transaction_t trans = {
        .length = 8,
        .tx_buffer = &cmd,
        .user = &panel->cmd_user,
    };

    return spi_device_polling_transmit(panel->spi_dev, &trans);
}

static esp_err_t gc9a01_send_data(gc9a01_handle_t panel, const void *data, size_t len)
{
    if (len == 0) {
        return ESP_OK;
    }

    spi_transaction_t trans = {
        .length = len * 8,
        .tx_buffer = data,
        .user = &panel->data_user,
    };

    return spi_device_polling_transmit(panel->spi_dev, &trans);
}

static esp_err_t gc9a01_set_window(gc9a01_handle_t panel, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    uint8_t data[4];

    ESP_RETURN_ON_ERROR(gc9a01_send_cmd(panel, GC9A01_CMD_CASET), TAG, "send CASET failed");
    data[0] = (uint8_t)(x1 >> 8);
    data[1] = (uint8_t)(x1 & 0xFF);
    data[2] = (uint8_t)(x2 >> 8);
    data[3] = (uint8_t)(x2 & 0xFF);
    ESP_RETURN_ON_ERROR(gc9a01_send_data(panel, data, sizeof(data)), TAG, "send CASET data failed");

    ESP_RETURN_ON_ERROR(gc9a01_send_cmd(panel, GC9A01_CMD_RASET), TAG, "send RASET failed");
    data[0] = (uint8_t)(y1 >> 8);
    data[1] = (uint8_t)(y1 & 0xFF);
    data[2] = (uint8_t)(y2 >> 8);
    data[3] = (uint8_t)(y2 & 0xFF);
    ESP_RETURN_ON_ERROR(gc9a01_send_data(panel, data, sizeof(data)), TAG, "send RASET data failed");

    return gc9a01_send_cmd(panel, GC9A01_CMD_RAMWR);
}

static void IRAM_ATTR gc9a01_spi_pre_cb(spi_transaction_t *trans)
{
    gc9a01_trans_user_t *user = (gc9a01_trans_user_t *)trans->user;
    struct gc9a01_device_t *panel = NULL;

    if (user == NULL) {
        return;
    }

    panel = user->panel;
    if (panel == NULL) {
        return;
    }

    if (user->type == GC9A01_TRANS_TYPE_DATA || user->type == GC9A01_TRANS_TYPE_COLOR) {
        gpio_set_level(panel->config.pin_dc, 1);
    } else {
        gpio_set_level(panel->config.pin_dc, 0);
    }
}

static void IRAM_ATTR gc9a01_spi_post_cb(spi_transaction_t *trans)
{
    gc9a01_trans_user_t *user = (gc9a01_trans_user_t *)trans->user;

    if (user != NULL && user->type == GC9A01_TRANS_TYPE_COLOR && user->done_cb != NULL) {
        user->done_cb(user->user_ctx);
    }
}
