#ifndef __CST816S_H_
#define __CST816S_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

typedef struct {
    i2c_port_num_t i2c_port;
    gpio_num_t pin_sda;
    gpio_num_t pin_scl;
	gpio_num_t pin_int;
    gpio_num_t pin_rst;
    uint16_t dev_addr;
    uint32_t scl_speed_hz;
    uint16_t x_max;
    uint16_t y_max;
} cst816s_config_t;

typedef struct {
    uint16_t x;
    uint16_t y;
    bool pressed;
} cst816s_point_t;

typedef struct cst816s_device_t *cst816s_handle_t;
i2c_master_bus_handle_t cst816s_get_i2c_bus(cst816s_handle_t handle);
esp_err_t cst816s_new(const cst816s_config_t *config, cst816s_handle_t *ret_touch);
esp_err_t cst816s_read_point(cst816s_handle_t touch, cst816s_point_t *point);

#ifdef __cplusplus
}
#endif

#endif
