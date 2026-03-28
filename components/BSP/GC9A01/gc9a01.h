#ifndef __GC9A01_H_
#define __GC9A01_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

typedef struct {
    spi_host_device_t spi_host;
    gpio_num_t pin_mosi;
    gpio_num_t pin_sclk;
    gpio_num_t pin_cs;
    gpio_num_t pin_dc;
    gpio_num_t pin_rst;
    gpio_num_t pin_blk;
    uint32_t pixel_clock_hz;
    uint16_t hor_res;
    uint16_t ver_res;
    size_t max_transfer_sz;
} gc9a01_config_t;

typedef struct gc9a01_device_t *gc9a01_handle_t;

typedef void (*gc9a01_color_trans_done_cb_t)(void *user_ctx);

esp_err_t gc9a01_new_panel(const gc9a01_config_t *config, gc9a01_handle_t *ret_panel);
esp_err_t gc9a01_reset(gc9a01_handle_t panel);
esp_err_t gc9a01_init(gc9a01_handle_t panel);
esp_err_t gc9a01_set_backlight(gc9a01_handle_t panel, bool on);
esp_err_t gc9a01_draw_bitmap(gc9a01_handle_t panel,
                             uint16_t x1,
                             uint16_t y1,
                             uint16_t x2,
                             uint16_t y2,
                             const void *color_data,
                             size_t color_size);
esp_err_t gc9a01_draw_bitmap_async(gc9a01_handle_t panel,
                                    uint16_t x1,
                                    uint16_t y1,
                                    uint16_t x2,
                                    uint16_t y2,
                                    const void *color_data,
                                    size_t color_size,
                                    gc9a01_color_trans_done_cb_t done_cb,
                                    void *user_ctx);

#ifdef __cplusplus
}
#endif

#endif
