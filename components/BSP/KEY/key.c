#include "key.h"

#define KEY_DEBOUNCE_MS 10

static TickType_t s_last_scan_tick;
static bool s_key_pressed;
static bool s_key_press_event;

static bool key_is_pressed_raw(void);

void key_Init(void)
{
    gpio_config_t gpio_init_struct = {0};

    gpio_init_struct.intr_type = GPIO_INTR_DISABLE;
    gpio_init_struct.mode = GPIO_MODE_INPUT;
    gpio_init_struct.pin_bit_mask = 1ULL << KEY_GPIO;
    gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_init_struct.pull_up_en = GPIO_PULLUP_ENABLE;

    gpio_config(&gpio_init_struct);

    s_last_scan_tick = xTaskGetTickCount();
    s_key_pressed = key_is_pressed_raw();
    s_key_press_event = false;
}

void key_proc(void)
{
    const TickType_t now = xTaskGetTickCount();
    const bool pressed = key_is_pressed_raw();

    if ((now - s_last_scan_tick) < pdMS_TO_TICKS(KEY_DEBOUNCE_MS)) {
        return;
    }

    s_last_scan_tick = now;

    if (pressed && !s_key_pressed) {
        s_key_press_event = true;
    }

    s_key_pressed = pressed;
}

bool key_get_press_event(void)
{
    const bool pressed = s_key_press_event;

    s_key_press_event = false;
    return pressed;
}

static bool key_is_pressed_raw(void)
{
    return gpio_get_level(KEY_GPIO) == 0;
}
