#ifndef __LED_H_
#define __LED_H_

#include "driver/gpio.h"

#define LED_GPIO GPIO_NUM_2

enum GPIO_OUTPUT_STAT
{
    PIN_RESET = 0, // 明确赋值 0 和 1 对于 GPIO 电平操作更安全
    PIN_SET = 1
};

#define LED(x) do { \
    (x) ? gpio_set_level(LED_GPIO, PIN_SET) : gpio_set_level(LED_GPIO, PIN_RESET); \
} while (0)

#define LED_TOGGLE() do { \
    gpio_set_level(LED_GPIO, !gpio_get_level(LED_GPIO)); \
} while (0)

void led_Init(void);


#endif
