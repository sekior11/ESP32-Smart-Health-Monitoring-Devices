#ifndef __KEY_H_
#define __KEY_H_

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define KEY_GPIO GPIO_NUM_0

void key_Init(void);
void key_proc(void);
bool key_get_press_event(void);

#endif
