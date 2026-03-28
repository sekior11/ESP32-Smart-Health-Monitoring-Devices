#ifndef LV_PORT_DISP_H
#define LV_PORT_DISP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "esp_err.h"
#include "lvgl.h"

void lv_port_disp_init(void);
lv_disp_t *lv_port_disp_get_handle(void);
esp_err_t lv_port_disp_set_backlight(bool on);

#ifdef __cplusplus
}
#endif

#endif
