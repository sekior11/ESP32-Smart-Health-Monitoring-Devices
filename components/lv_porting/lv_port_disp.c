#include "lv_port_disp.h"

#include <string.h>

#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "gc9a01.h"

#define GC9A01_HOR_RES              240
#define GC9A01_VER_RES              240
#define GC9A01_BUF_LINES            40

#define GC9A01_PIN_MOSI             GPIO_NUM_23
#define GC9A01_PIN_SCLK             GPIO_NUM_18
#define GC9A01_PIN_CS               GPIO_NUM_5
#define GC9A01_PIN_DC               GPIO_NUM_2
#define GC9A01_PIN_RST              GPIO_NUM_4
#define GC9A01_PIN_BLK              GPIO_NUM_15

#define GC9A01_SPI_HOST             SPI2_HOST
#define GC9A01_SPI_CLOCK_HZ         (40 * 1000 * 1000)

static const char *TAG = "lv_port_disp";

static gc9a01_handle_t s_panel_handle;
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;
static lv_disp_t *s_disp;
static lv_color_t *s_buf1;
static lv_color_t *s_buf2;

static void my_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p);
static void rgb565_swap_inplace(lv_color_t *color_p, uint32_t pixel_count);

void lv_port_disp_init(void)
{
    const size_t buf_pixels = GC9A01_HOR_RES * GC9A01_BUF_LINES;
    const size_t buf_size = buf_pixels * sizeof(lv_color_t);

    gc9a01_config_t panel_config = {
        .spi_host = GC9A01_SPI_HOST,
        .pin_mosi = GC9A01_PIN_MOSI,
        .pin_sclk = GC9A01_PIN_SCLK,
        .pin_cs = GC9A01_PIN_CS,
        .pin_dc = GC9A01_PIN_DC,
        .pin_rst = GC9A01_PIN_RST,
        .pin_blk = GC9A01_PIN_BLK,
        .pixel_clock_hz = GC9A01_SPI_CLOCK_HZ,
        .hor_res = GC9A01_HOR_RES,
        .ver_res = GC9A01_VER_RES,
        .max_transfer_sz = GC9A01_HOR_RES * GC9A01_BUF_LINES * sizeof(lv_color_t) + 16,
    };

    ESP_ERROR_CHECK(gc9a01_new_panel(&panel_config, &s_panel_handle));
    ESP_ERROR_CHECK(gc9a01_reset(s_panel_handle));
    ESP_ERROR_CHECK(gc9a01_init(s_panel_handle));
    ESP_ERROR_CHECK(gc9a01_set_backlight(s_panel_handle, false));

    /*
     * 这里必须使用 DMA 可访问内存创建双缓冲。
     * LVGL 在 buf1 刷新的同时，会把下一块区域渲染到 buf2；
     * SPI DMA 再直接从这两块内存里读取像素数据发给 GC9A01。
     */
    s_buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    s_buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_ERROR_CHECK(s_buf1 ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(s_buf2 ? ESP_OK : ESP_ERR_NO_MEM);

    memset(s_buf1, 0, buf_size);
    memset(s_buf2, 0, buf_size);

    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, buf_pixels);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = GC9A01_HOR_RES;
    s_disp_drv.ver_res = GC9A01_VER_RES;
    s_disp_drv.flush_cb = my_disp_flush;
    s_disp_drv.draw_buf = &s_draw_buf;

    s_disp = lv_disp_drv_register(&s_disp_drv);
    ESP_ERROR_CHECK(s_disp ? ESP_OK : ESP_FAIL);

    ESP_LOGI(TAG, "LVGL display port ready");
}

lv_disp_t *lv_port_disp_get_handle(void)
{
    return s_disp;
}

esp_err_t lv_port_disp_set_backlight(bool on)
{
    if (s_panel_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return gc9a01_set_backlight(s_panel_handle, on);
}

static void my_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    const uint32_t width = (uint32_t)(area->x2 - area->x1 + 1);
    const uint32_t height = (uint32_t)(area->y2 - area->y1 + 1);
    const uint32_t pixel_count = width * height;
    const size_t color_size = pixel_count * sizeof(lv_color_t);

    if (area->x2 < 0 || area->y2 < 0 || area->x1 > (GC9A01_HOR_RES - 1) || area->y1 > (GC9A01_VER_RES - 1)) {
        lv_disp_flush_ready(disp_drv);
        return;
    }

    /*
     * GC9A01 通过 SPI 接收 RGB565 时，常见模块要求按高字节在前发送。
     * 但当前工程的 LVGL 配置里 `LV_COLOR_16_SWAP=0`，
     * 在 ESP32 这类小端 CPU 上，显存里的 16 位像素天然是低字节在前。
     * 因此这里在发送前原地交换每个像素的两个字节，避免出现黑屏/花屏/颜色异常。
     */
rgb565_swap_inplace(color_p, pixel_count);
    /*
     * 真正的 SPI 刷新放在独立的 GC9A01 驱动里处理：
     * 1. 设置窗口地址
     * 2. 发送 RAMWR
     * 3. 以 DMA 异步方式发送 color_p 指向的像素块
     * 4. 传输完成后在回调里执行 lv_disp_flush_ready()
     */
    esp_err_t ret = gc9a01_draw_bitmap(s_panel_handle,
                                       (uint16_t)area->x1,
                                       (uint16_t)area->y1,
                                       (uint16_t)area->x2,
                                       (uint16_t)area->y2,
                                       color_p,
                                       color_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "flush failed: %s", esp_err_to_name(ret));
    }

    lv_disp_flush_ready(disp_drv);
}

static void rgb565_swap_inplace(lv_color_t *color_p, uint32_t pixel_count)
{
#if LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP == 0
    uint16_t *buf16 = (uint16_t *)color_p;

    for (uint32_t i = 0; i < pixel_count; i++) {
        uint16_t c = buf16[i];
        buf16[i] = (uint16_t)((c << 8) | (c >> 8));
    }
#else
    LV_UNUSED(color_p);
    LV_UNUSED(pixel_count);
#endif
}
