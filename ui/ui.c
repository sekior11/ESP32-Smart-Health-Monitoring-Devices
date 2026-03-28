#include "ui.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "business.h"

#define TAG "ui"

// 全局页面变量
app_page_t current_page = PAGE_HOME;

// 屏幕对象
lv_obj_t *s_home_screen;
lv_obj_t *settings_screen;
lv_obj_t *health_screen;
lv_obj_t *game_screen;
lv_obj_t *weather_screen;
lv_obj_t *bluetooth_screen;

// 健康监测相关UI元素
lv_obj_t *ppg_chart;
lv_chart_series_t *ser_hr;
lv_chart_series_t *ser_spo2;
lv_obj_t *label_hr_val;
lv_obj_t *label_spo2_val;
lv_timer_t *ppg_timer;

// 游戏相关UI元素
lv_obj_t *game_ball;
lv_obj_t *game_coin;
lv_obj_t *label_score;
lv_timer_t *game_timer;

// 天气相关UI元素
lv_obj_t *label_city;
lv_obj_t *label_temp;
lv_obj_t *label_condition;
lv_obj_t *label_humidity;
lv_obj_t *weather_img;

// 主屏幕相关UI元素
lv_obj_t *arc_hr;
lv_obj_t *arc_spo2;
lv_obj_t *label_weather;
lv_obj_t *label_time;
lv_obj_t *label_date;
lv_obj_t *label_hr_icon;
lv_obj_t *label_hr_value;
lv_obj_t *label_spo2_icon;
lv_obj_t *label_spo2_value;
lv_obj_t *hint_label;

// 触摸相关
static volatile lv_dir_t s_pending_swipe_dir = LV_DIR_NONE;
static int64_t s_ignore_nav_click_until_us = 0;

// 游戏相关变量
float ball_x = 110.0f;
float ball_y = 110.0f;
float ball_vx = 0.0f;
float ball_vy = 0.0f;
int game_score = 0;

// LVGL定时器句柄
static esp_timer_handle_t s_lvgl_tick_timer;

// LVGL字体声明
LV_FONT_DECLARE(my_font_16);

/**
 * @brief 初始化UI
 * 
 * 该函数负责初始化LVGL UI系统，包括：
 * 1. 初始化LVGL库
 * 2. 初始化显示和输入设备
 * 3. 创建所有屏幕
 * 4. 加载主屏幕
 * 5. 创建LVGL定时器
 * 6. 预热显示
 * 7. 创建LVGL任务
 * 
 * @return esp_err_t - 操作结果，成功返回ESP_OK
 */
esp_err_t ui_init(void)
{
    // 初始化LVGL
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();
    
    // 创建所有屏幕
    create_home_screen();
    create_settings_screen();
    create_health_screen();
    create_game_screen();
    create_weather_screen();
    create_bluetooth_screen();
    
    // 加载主屏幕
    lv_scr_load(s_home_screen);
    
    // 创建LVGL定时器
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = lvgl_tick_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "lvgl_tick",
        .skip_unhandled_events = true,
    };
    
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &s_lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));
    
    // 预热显示
    warm_up_display();
    ESP_ERROR_CHECK(lv_port_disp_set_backlight(true));
    
    // 创建LVGL任务
    ESP_ERROR_CHECK(xTaskCreate(lvgl_task,
                                "lvgl_task",
                                LVGL_TASK_STACK_SIZE,
                                NULL,
                                LVGL_TASK_PRIORITY,
                                NULL) == pdPASS ? ESP_OK : ESP_FAIL);
    
    ESP_LOGI(TAG, "LVGL UI initialized");
    return ESP_OK;
}

// 前向声明
static void lvgl_tick_cb_internal(void *arg);
static void lvgl_task_internal(void *arg);

// LVGL定时器回调（内部实现）
static void lvgl_tick_cb_internal(void *arg)
{
    LV_UNUSED(arg);
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

// LVGL任务（内部实现）
static void lvgl_task_internal(void *arg)
{
    LV_UNUSED(arg);

    while (1) {
        uint32_t delay_ms = lv_timer_handler();

        if (s_pending_swipe_dir != LV_DIR_NONE) {
            lv_dir_t dir = s_pending_swipe_dir;
            s_pending_swipe_dir = LV_DIR_NONE;
            handle_swipe_dir(dir);
        }

        if (delay_ms == 0 || delay_ms > LVGL_TASK_MAX_DELAY_MS) {
            delay_ms = LVGL_TASK_MAX_DELAY_MS;
        }

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

/**
 * @brief LVGL定时器回调（外部接口）
 * 
 * 该函数是LVGL定时器的回调函数，用于更新LVGL的系统时间。
 * 
 * @param arg - 回调参数（未使用）
 */
void lvgl_tick_cb(void *arg)
{
    lvgl_tick_cb_internal(arg);
}

/**
 * @brief LVGL任务（外部接口）
 * 
 * 该函数是LVGL的主任务，负责处理LVGL的事件循环和界面更新。
 * 
 * @param arg - 任务参数（未使用）
 */
void lvgl_task(void *arg)
{
    lvgl_task_internal(arg);
}

/**
 * @brief 处理滑动方向
 * 
 * 该函数负责处理屏幕滑动事件，根据滑动方向切换不同的页面。
 * 
 * @param dir - 滑动方向
 */
static void handle_swipe_dir(lv_dir_t dir)
{
    if ((current_page == PAGE_HOME) && (dir == LV_DIR_LEFT)) {
        s_ignore_nav_click_until_us = esp_timer_get_time() + 350000;
        load_page(PAGE_SETTINGS);
    } else if ((current_page == PAGE_SETTINGS) && (dir == LV_DIR_RIGHT)) {
        s_ignore_nav_click_until_us = esp_timer_get_time() + 350000;
        load_page(PAGE_HOME);
    }
}

/**
 * @brief 加载页面
 * 
 * 该函数负责加载指定的页面，包括：
 * 1. 根据页面类型选择目标屏幕
 * 2. 更新当前页面状态
 * 3. 切换到目标屏幕
 * 
 * @param page - 要加载的页面类型
 */
static void load_page(app_page_t page)
{
    lv_obj_t *target_screen = NULL;

    switch (page) {
        case PAGE_HOME:      target_screen = s_home_screen; break;
        case PAGE_SETTINGS:  target_screen = settings_screen; break;
        case PAGE_HEALTH:    target_screen = health_screen; break;
        case PAGE_GAME:      target_screen = game_screen; break;
        case PAGE_WEATHER:   update_weather_ui(); target_screen = weather_screen; break;
        case PAGE_BLUETOOTH: target_screen = bluetooth_screen; break;
        default: return;
    }

    if (target_screen == NULL || lv_scr_act() == target_screen) {
        return;
    }

    current_page = page;
    
    // 采用无动画切换，解决卡顿
    lv_scr_load_anim(target_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    ESP_LOGI(TAG, "Switched to page %d", page);
}

/**
 * @brief 页面导航事件回调
 * 
 * 该函数是页面导航按钮的事件回调函数，用于处理按钮点击事件并加载目标页面。
 * 
 * @param e - 事件对象
 */
static void page_nav_event_cb(lv_event_t *e)
{
    if (esp_timer_get_time() < s_ignore_nav_click_until_us) {
        return;
    }

    app_page_t target_page = (app_page_t)(uintptr_t)lv_event_get_user_data(e);
    load_page(target_page);
}

/**
 * @brief 创建导航按钮
 * 
 * 该函数负责创建导航按钮，包括：
 * 1. 创建按钮对象
 * 2. 设置按钮样式
 * 3. 添加点击事件回调
 * 4. 创建按钮文本标签
 * 
 * @param parent - 父对象
 * @param text - 按钮文本
 * @param align - 对齐方式
 * @param x_ofs - X轴偏移量
 * @param y_ofs - Y轴偏移量
 * @param target_page - 目标页面
 */
static void create_nav_button(lv_obj_t *parent, const char *text, lv_align_t align, lv_coord_t x_ofs, lv_coord_t y_ofs, app_page_t target_page)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_t *label = lv_label_create(btn);

    lv_obj_set_size(btn, NAV_BTN_WIDTH, NAV_BTN_HEIGHT);
    lv_obj_align(btn, align, x_ofs, y_ofs);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x4A4A4A), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x161616), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x303030), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, page_nav_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)target_page);

    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_center(label);
}

/**
 * @brief 创建菜单项
 * 
 * 该函数负责创建菜单项，包括：
 * 1. 创建按钮对象
 * 2. 设置按钮样式
 * 3. 添加点击事件回调
 * 4. 创建按钮文本标签
 * 
 * @param parent - 父对象
 * @param text - 菜单项文本
 * @param y_ofs - Y轴偏移量
 * @param target_page - 目标页面
 * @return lv_obj_t* - 创建的菜单项对象
 */
static lv_obj_t *create_menu_item(lv_obj_t *parent, const char *text, lv_coord_t y_ofs, app_page_t target_page)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_t *label = lv_label_create(btn);

    lv_obj_set_size(btn, SETTINGS_ITEM_WIDTH, SETTINGS_ITEM_HEIGHT);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, y_ofs);
    lv_obj_set_style_radius(btn, 18, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x414141), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2E2E2E), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, page_nav_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)target_page);

    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_center(label);

    return btn;
}

/**
 * @brief 创建主屏幕
 * 
 * 该函数负责创建主屏幕，包括：
 * 1. 创建屏幕对象
 * 2. 创建血氧和心率圆弧
 * 3. 创建天气、时间、日期显示
 * 4. 创建心率和血氧数值显示
 * 5. 创建提示文本
 */
static void create_home_screen(void)
{
    s_home_screen = create_screen(lv_color_black());

    // 外圈：血氧圆弧
    arc_spo2 = lv_arc_create(s_home_screen);
    lv_obj_set_size(arc_spo2, 220, 220);
    lv_arc_set_rotation(arc_spo2, 270);
    lv_arc_set_bg_angles(arc_spo2, 0, 360);
    lv_arc_set_range(arc_spo2, 0, 100);
    lv_arc_set_value(arc_spo2, (int16_t)final_spo2);
    lv_obj_remove_style(arc_spo2, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc_spo2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(arc_spo2, lv_color_hex(0x4D0000), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_spo2, lv_color_hex(0xFF4D4D), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_spo2, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_spo2, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc_spo2, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc_spo2, true, LV_PART_INDICATOR);
    lv_obj_center(arc_spo2);

    // 内圈：心率圆弧
    arc_hr = lv_arc_create(s_home_screen);
    lv_obj_set_size(arc_hr, 190, 190);
    lv_arc_set_rotation(arc_hr, 270);
    lv_arc_set_bg_angles(arc_hr, 0, 360);
    lv_arc_set_range(arc_hr, 0, 200);
    lv_arc_set_value(arc_hr, (int16_t)final_hr);
    lv_obj_remove_style(arc_hr, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc_hr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(arc_hr, lv_color_hex(0x001A4D), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_hr, lv_color_hex(0x3399FF), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_hr, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_hr, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc_hr, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc_hr, true, LV_PART_INDICATOR);
    lv_obj_center(arc_hr);

    // 顶部：真实天气
    label_weather = lv_label_create(s_home_screen);
    lv_obj_set_style_text_color(label_weather, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_weather, &my_font_16, 0);
    lv_label_set_text_fmt(label_weather, "%s %d C", g_weather_condition, g_weather_temp);
    lv_obj_align(label_weather, LV_ALIGN_TOP_MID, 0, 80);

    // 中心：时间与日期
    label_time = lv_label_create(s_home_screen);
    lv_label_set_text(label_time, "10:08");
    lv_obj_set_style_text_color(label_time, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_time, &lv_font_montserrat_14, 0);
    lv_obj_set_style_transform_zoom(label_time, 512, 0);
    lv_obj_align(label_time, LV_ALIGN_CENTER, 0, -25);

    label_date = lv_label_create(s_home_screen);
    lv_label_set_text(label_date, "MON, OCT 24");
    lv_obj_set_style_text_color(label_date, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(label_date, &lv_font_montserrat_14, 0);
    lv_obj_align(label_date, LV_ALIGN_CENTER, 0, 10);

    // 底部右侧：心率数值
    label_hr_icon = lv_label_create(s_home_screen);
    lv_label_set_text(label_hr_icon, "HR");
    lv_obj_set_style_text_color(label_hr_icon, lv_color_hex(0x3399FF), 0);
    lv_obj_align(label_hr_icon, LV_ALIGN_CENTER, -45, 50);

    label_hr_value = lv_label_create(s_home_screen);
    lv_label_set_text_fmt(label_hr_value, "%ld", final_hr);
    lv_obj_set_style_text_color(label_hr_value, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_hr_value, &lv_font_montserrat_14, 0);
    lv_obj_align_to(label_hr_value, label_hr_icon, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

    // 底部左侧：血氧数值
    label_spo2_icon = lv_label_create(s_home_screen);
    lv_label_set_text(label_spo2_icon, "SpO2");
    lv_obj_set_style_text_color(label_spo2_icon, lv_color_hex(0xFF4D4D), 0);
    lv_obj_align(label_spo2_icon, LV_ALIGN_CENTER, 15, 50);

    label_spo2_value = lv_label_create(s_home_screen);
    lv_label_set_text_fmt(label_spo2_value, "%d%%", (int)final_spo2);
    lv_obj_set_style_text_color(label_spo2_value, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_spo2_value, &lv_font_montserrat_14, 0);
    lv_obj_align_to(label_spo2_value, label_spo2_icon, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

    // 提示文本
    hint_label = lv_label_create(s_home_screen);
    lv_label_set_text(hint_label, "Swipe left for menu");
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x555555), 0);
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_12, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -10);

}

/**
 * @brief 创建设置屏幕
 * 
 * 该函数负责创建设置屏幕，包括：
 * 1. 创建屏幕对象
 * 2. 创建顶部背景和标题
 * 3. 创建导航按钮和菜单项
 */
static void create_settings_screen(void)
{
    lv_obj_t *top_bg;
    lv_obj_t *title_label;

    settings_screen = create_screen(lv_color_black());

    top_bg = lv_obj_create(settings_screen);
    lv_obj_set_size(top_bg, 240, 60);
    lv_obj_align(top_bg, LV_ALIGN_TOP_MID, 0, -20);
    lv_obj_set_style_radius(top_bg, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(top_bg, 0, 0);
    lv_obj_set_style_bg_color(top_bg, lv_color_hex(0xD3D3D3), 0);

    title_label = lv_label_create(settings_screen);
    lv_label_set_text(title_label, "Settings");
    lv_obj_set_style_text_color(title_label, lv_color_black(), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_16, 0);
    lv_obj_align_to(title_label, top_bg, LV_ALIGN_BOTTOM_MID, 0, 0);

    create_nav_button(settings_screen, "Home", LV_ALIGN_TOP_LEFT, -1, 100, PAGE_HOME);
    create_menu_item(settings_screen, "Weather", 55, PAGE_WEATHER);
    create_menu_item(settings_screen, "Health Check", 95, PAGE_HEALTH);
    create_menu_item(settings_screen, "Game", 135, PAGE_GAME);
    create_menu_item(settings_screen, "Bluetooth", 175, PAGE_BLUETOOTH);
}

/**
 * @brief 创建健康监测屏幕
 * 
 * 该函数负责创建健康监测屏幕，包括：
 * 1. 创建屏幕对象
 * 2. 创建导航按钮
 * 3. 创建标题
 * 4. 创建健康数据图表
 * 5. 创建心率和血氧数值显示
 * 6. 创建数据更新定时器
 */
static void create_health_screen(void)
{
    lv_obj_t *title;

    health_screen = create_screen(lv_color_black());
    create_nav_button(health_screen, "Home", LV_ALIGN_TOP_LEFT,60, 165, PAGE_HOME);
    create_nav_button(health_screen, "Menu", LV_ALIGN_TOP_RIGHT,-60, 165, PAGE_SETTINGS);

    title = lv_label_create(health_screen);
    lv_label_set_text(title, "Health Check");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    ppg_chart = lv_chart_create(health_screen);
    lv_obj_set_size(ppg_chart, 220, 110);
    lv_obj_align(ppg_chart, LV_ALIGN_CENTER, 0, -10);
    lv_chart_set_type(ppg_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(ppg_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_point_count(ppg_chart, 100);
    lv_chart_set_range(ppg_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_obj_set_style_bg_opa(ppg_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ppg_chart, 0, 0);
    lv_obj_set_style_line_width(ppg_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(ppg_chart, 0, LV_PART_INDICATOR);

    ser_hr = lv_chart_add_series(ppg_chart, lv_color_hex(0xFF4D4D), LV_CHART_AXIS_PRIMARY_Y);
    ser_spo2 = lv_chart_add_series(ppg_chart, lv_color_hex(0x00E5FF), LV_CHART_AXIS_PRIMARY_Y);

    label_hr_val = lv_label_create(health_screen);
    lv_label_set_text(label_hr_val, "HR 85");
    lv_obj_set_style_text_color(label_hr_val, lv_color_hex(0xFF4D4D), 0);
    lv_obj_set_style_text_font(label_hr_val, &lv_font_montserrat_20, 0);
    lv_obj_align(label_hr_val, LV_ALIGN_BOTTOM_LEFT, 30, -25);

    label_spo2_val = lv_label_create(health_screen);
    lv_label_set_text(label_spo2_val, "O2 98%");
    lv_obj_set_style_text_color(label_spo2_val, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(label_spo2_val, &lv_font_montserrat_20, 0);
    lv_obj_align(label_spo2_val, LV_ALIGN_BOTTOM_RIGHT, -40, -25);

    ppg_timer = lv_timer_create(ppg_timer_cb, 40, NULL);
}

/**
 * @brief 健康监测数据更新回调
 * 
 * 该函数是健康监测数据更新的定时器回调函数，包括：
 * 1. 刷新健康页面的折线图表
 * 2. 定期更新所有界面的健康数据数值
 * 
 * @param timer - 定时器对象
 */
static void ppg_timer_cb(lv_timer_t *timer)
{
    static uint32_t t = 0;
    t++;

    // 1. 只有在【健康页面】时，才刷新折线图表
    if (current_page == PAGE_HEALTH) {
        int16_t hr_wave = final_hr;
        int16_t spo2_wave = final_spo2;

        if (hr_wave < 0) hr_wave = 0;
        if (hr_wave > 100) hr_wave = 100;
        if (spo2_wave < 0) spo2_wave = 0;
        if (spo2_wave > 100) spo2_wave = 100;

        lv_chart_set_next_value(ppg_chart, ser_hr, hr_wave);
        lv_chart_set_next_value(ppg_chart, ser_spo2, spo2_wave); 
    }

    // 2. 无论在哪个页面，每 50 步 (约2秒) 都要刷新一次所有界面的【数值】
    if (t % 50 == 0) {
        
        // 更新健康界面的文本
        if (label_hr_val != NULL) {
            lv_label_set_text_fmt(label_hr_val, "HR %ld", final_hr);
            lv_label_set_text_fmt(label_spo2_val, "O2 %d%%", (int)final_spo2);
        }

        // 更新主界面的数据
        if (s_home_screen != NULL) { 
            // 更新两个圆弧进度条
            lv_arc_set_value(arc_spo2, (int16_t)final_spo2);
            lv_arc_set_value(arc_hr, (int16_t)final_hr);
            
            // 更新数字
            lv_label_set_text_fmt(label_hr_value, "%ld", final_hr);
            lv_label_set_text_fmt(label_spo2_value, "%d%%", (int)final_spo2);
            
            // 更新天气
            lv_label_set_text_fmt(label_weather, "%s %d C", g_weather_condition, g_weather_temp);
        }
    }
}

/**
 * @brief 重新生成硬币位置
 * 
 * 该函数负责在游戏屏幕上随机生成硬币的位置。
 */
void respawn_coin(void)
{
    int cx = 20 + (rand() % 180);
    int cy = 20 + (rand() % 180);

    lv_obj_set_pos(game_coin, cx, cy);
}

/**
 * @brief 游戏定时器回调
 * 
 * 该函数是游戏逻辑的定时器回调函数，包括：
 * 1. 读取MPU6050数据
 * 2. 更新小球位置和速度
 * 3. 检测碰撞和边界
 * 4. 检测硬币收集
 * 
 * @param timer - 定时器对象
 */
static void game_timer_cb(lv_timer_t *timer)
{
    float pitch;
    float roll;
    int cx;
    int cy;

    LV_UNUSED(timer);

    if (current_page != PAGE_GAME) {
        return;
    }

    get_mpu6050_data(&pitch, &roll);

    //小球处理数据
    ball_vx += roll * 0.1f;   
    ball_vy += pitch * 0.1f;  

    ball_x += ball_vx;
    ball_y += ball_vy;
    // 1. 计算小球真实的中心坐标 (加上小球半径 10)
    float ball_center_x = ball_x + 10.0f;
    float ball_center_y = ball_y + 10.0f;

    // 2. 计算小球中心到屏幕中心 (120, 120) 的 X 和 Y 偏差量
    float dx = ball_center_x - 120.0f;
    float dy = ball_center_y - 120.0f;

    // 3. 计算距离的平方
    float distance_sq = dx * dx + dy * dy;

    // 4. 判断是否超出最大活动半径 (屏幕半径 120 - 小球半径 10 = 110)
    // 110 的平方是 12100
    if (distance_sq >= 12100.0f) {
        // 算出真实的直线距离
        float distance = sqrtf(distance_sq);
        
        // 核心动作 1：防粘墙，把小球强行拔回圆内
        float overlap = distance - 110.0f; // 算出小球陷入墙壁有多深
        ball_x -= (dx / distance) * overlap; 
        ball_y -= (dy / distance) * overlap;

        // 核心动作 2：向量镜面反弹 (真实的撞球物理效果)
        // 算出法向量 (垂直于墙面的力)
        float nx = dx / distance;
        float ny = dy / distance;
        // 算出速度在法向量上的投影 (V·N)
        float v_dot_n = ball_vx * nx + ball_vy * ny;

        // 只有当小球在向外冲的时候才反弹，乘以 0.6f 模拟撞击能量损耗
        if (v_dot_n > 0) {
            ball_vx = (ball_vx - 2.0f * v_dot_n * nx) * 0.6f;
            ball_vy = (ball_vy - 2.0f * v_dot_n * ny) * 0.6f;
        }
    }

    lv_obj_set_pos(game_ball, (int)ball_x, (int)ball_y);

    cx = lv_obj_get_x(game_coin);
    cy = lv_obj_get_y(game_coin);
    if (abs((int)ball_x - cx) < 20 && abs((int)ball_y - cy) < 20) {
        game_score += 10;
        lv_label_set_text_fmt(label_score, "Score: %d", game_score);
        respawn_coin();
    }
}

/**
 * @brief 创建游戏屏幕
 * 
 * 该函数负责创建游戏屏幕，包括：
 * 1. 创建屏幕对象
 * 2. 创建导航按钮
 * 3. 创建分数显示
 * 4. 创建硬币和小球
 * 5. 创建游戏定时器
 */
static void create_game_screen(void)
{
    game_screen = create_screen(lv_color_black());
    create_nav_button(game_screen, "Home", LV_ALIGN_TOP_LEFT, 5, 100, PAGE_HOME);
    create_nav_button(game_screen, "Menu", LV_ALIGN_TOP_RIGHT, -5, 100, PAGE_SETTINGS);

    label_score = lv_label_create(game_screen);
    lv_label_set_text(label_score, "Score: 0");
    lv_obj_set_style_text_color(label_score, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_score, &lv_font_montserrat_20, 0);
    lv_obj_align(label_score, LV_ALIGN_TOP_MID, 0, 16);

    game_coin = lv_obj_create(game_screen);
    lv_obj_set_size(game_coin, 16, 16);
    lv_obj_set_style_radius(game_coin, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(game_coin, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_border_width(game_coin, 0, 0);
    respawn_coin();

    game_ball = lv_obj_create(game_screen);
    lv_obj_set_size(game_ball, 20, 20);
    lv_obj_set_style_radius(game_ball, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(game_ball, lv_color_hex(0xFFD700), 0);
    lv_obj_set_style_border_width(game_ball, 0, 0);
    lv_obj_set_pos(game_ball, (int)ball_x, (int)ball_y);

    game_timer = lv_timer_create(game_timer_cb, 30, NULL);
}

/**
 * @brief 更新天气UI
 * 
 * 该函数负责更新天气屏幕上的天气数据显示，包括城市、温度、天气状况和湿度。
 */
void update_weather_ui(void)
{
    lv_label_set_text_fmt(label_city, "%s", g_weather_city);
    lv_label_set_text_fmt(label_temp, "%d C", g_weather_temp);
    lv_label_set_text_fmt(label_condition, "%s", g_weather_condition);
    lv_label_set_text_fmt(label_humidity, "Humidity: %d%%", g_weather_humidity);
}

/**
 * @brief 创建天气屏幕
 * 
 * 该函数负责创建天气屏幕，包括：
 * 1. 创建屏幕对象
 * 2. 创建导航按钮
 * 3. 创建城市、温度、天气状况和湿度显示
 * 4. 更新天气数据
 */
static void create_weather_screen(void)
{
    weather_screen = create_screen(lv_color_black());
    create_nav_button(weather_screen, "Home", LV_ALIGN_TOP_LEFT, 5, 100, PAGE_HOME);
    create_nav_button(weather_screen, "Menu", LV_ALIGN_TOP_RIGHT, -5, 100, PAGE_SETTINGS);

    // 1. 修改城市名称的字体
    label_city = lv_label_create(weather_screen);
    lv_obj_set_style_text_color(label_city, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_city, &my_font_16, 0);
    lv_obj_align(label_city, LV_ALIGN_TOP_MID, 0, 48);

    label_temp = lv_label_create(weather_screen);
    lv_obj_set_style_text_color(label_temp, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_temp, &lv_font_montserrat_20, 0);
    lv_obj_align(label_temp, LV_ALIGN_CENTER, 0, -20);

    // 2. 修改天气状况（晴/多云）的字体
    label_condition = lv_label_create(weather_screen);
    lv_obj_set_style_text_color(label_condition, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(label_condition, &my_font_16, 0);
    lv_obj_align(label_condition, LV_ALIGN_CENTER, 0, 40);

    // 3. 修改湿度的字体
    label_humidity = lv_label_create(weather_screen);
    lv_obj_set_style_text_color(label_humidity, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(label_humidity, &lv_font_simsun_16_cjk, 0);
    lv_obj_align(label_humidity, LV_ALIGN_BOTTOM_MID, 0, -30);

    update_weather_ui();
}

/**
 * @brief 蓝牙连接事件回调
 * 
 * 该函数是蓝牙连接按钮的事件回调函数，用于处理连接蓝牙的操作。
 * 
 * @param e - 事件对象
 */
static void btn_connect_event_cb(lv_event_t * e)
{
    // 获取绑定的自定义数据
    lv_obj_t * hint_label = (lv_obj_t *)lv_event_get_user_data(e);
    
    ESP_LOGI(TAG, "Connect 按键被按下！开启蓝牙...");
    if (hint_label) {
        lv_label_set_text(hint_label, "Status: Advertising...");
    }
    
    // 调用底层开启蓝牙的接口
    ble_beacon_start(); 
}

/**
 * @brief 蓝牙断开连接事件回调
 * 
 * 该函数是蓝牙断开连接按钮的事件回调函数，用于处理断开蓝牙的操作。
 * 
 * @param e - 事件对象
 */
static void btn_disconnect_event_cb(lv_event_t * e)
{
    // 获取绑定的自定义数据
    lv_obj_t * hint_label = (lv_obj_t *)lv_event_get_user_data(e);
    
    ESP_LOGI(TAG, "Disconnect 按键被按下！断开蓝牙...");
    if (hint_label) {
        lv_label_set_text(hint_label, "Status: Disconnected");
    }
    
    // 调用底层断开蓝牙的接口
    ble_beacon_stop();
}

/**
 * @brief 创建蓝牙屏幕
 * 
 * 该函数负责创建蓝牙屏幕，包括：
 * 1. 创建屏幕对象
 * 2. 创建导航按钮
 * 3. 创建标题
 * 4. 创建连接和断开按钮
 * 5. 创建状态提示
 */
static void create_bluetooth_screen(void)
{
    lv_obj_t *title;
    lv_obj_t *hint_label;
    
    // 声明两个按钮和它们内部的文字标签
    lv_obj_t *btn_connect;
    lv_obj_t *label_connect;
    lv_obj_t *btn_disconnect;
    lv_obj_t *label_disconnect;

    bluetooth_screen = create_screen(lv_color_black());
    
    // 保持左右两侧的导航按钮不变
    create_nav_button(bluetooth_screen, "Home", LV_ALIGN_TOP_LEFT, 60, 165, PAGE_HOME);
    create_nav_button(bluetooth_screen, "Menu", LV_ALIGN_TOP_RIGHT, -60, 165, PAGE_SETTINGS);

    // 页面标题
    title = lv_label_create(bluetooth_screen);
    lv_label_set_text(title, "Bluetooth");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    // 新增：Connect (连接) 绿色按钮
    btn_connect = lv_btn_create(bluetooth_screen);
    lv_obj_set_size(btn_connect, 130, 35);
    lv_obj_align(btn_connect, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_color(btn_connect, lv_color_hex(0x28A745), 0);
    lv_obj_set_style_radius(btn_connect, 10, 0);

    label_connect = lv_label_create(btn_connect);
    lv_label_set_text(label_connect, "Connect");
    lv_obj_set_style_text_font(label_connect, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(label_connect, lv_color_white(), 0);
    lv_obj_center(label_connect);

    // 新增：Disconnect (断开) 红色按钮
    btn_disconnect = lv_btn_create(bluetooth_screen);
    lv_obj_set_size(btn_disconnect, 130, 35);
    lv_obj_align(btn_disconnect, LV_ALIGN_CENTER, 0, 25);
    lv_obj_set_style_bg_color(btn_disconnect, lv_color_hex(0xDC3545), 0);
    lv_obj_set_style_radius(btn_disconnect, 10, 0);

    label_disconnect = lv_label_create(btn_disconnect);
    lv_label_set_text(label_disconnect, "Disconnect");
    lv_obj_set_style_text_font(label_disconnect, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(label_disconnect, lv_color_white(), 0);
    lv_obj_center(label_disconnect);

    // 可选：状态提示小字（放在标题下方，按钮上方）
    hint_label = lv_label_create(bluetooth_screen);
    lv_label_set_text(hint_label, "Status: Ready"); 
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0xA0A0A0), 0);
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_CENTER, 0, -60);

    lv_obj_add_event_cb(btn_connect, btn_connect_event_cb, LV_EVENT_CLICKED, hint_label);
    lv_obj_add_event_cb(btn_disconnect, btn_disconnect_event_cb, LV_EVENT_CLICKED, hint_label);

}

/**
 * @brief 预热显示
 * 
 * 该函数负责预热显示，确保LVGL界面正常初始化。
 */
static void warm_up_display(void)
{
    for (int i = 0; i < LVGL_STARTUP_FRAME_COUNT; i++) {
        (void)lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(LVGL_STARTUP_FRAME_DELAY_MS));
    }
}

/**
 * @brief 创建屏幕
 * 
 * 该函数负责创建一个新的屏幕对象，包括：
 * 1. 创建屏幕对象
 * 2. 设置屏幕属性
 * 3. 绑定滑动事件回调
 * 
 * @param bg_color - 屏幕背景颜色
 * @return lv_obj_t* - 创建的屏幕对象
 */
static lv_obj_t *create_screen(lv_color_t bg_color)
{
    lv_obj_t *screen = lv_obj_create(NULL);

    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_bg_color(screen, bg_color, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    
    // 给屏幕绑定滑动事件
    lv_obj_add_event_cb(screen, screen_gesture_event_cb, LV_EVENT_GESTURE, NULL);

    return screen;
}

/**
 * @brief 屏幕手势事件回调
 * 
 * 该函数是屏幕手势事件的回调函数，用于处理滑动操作。
 * 
 * @param e - 事件对象
 */
static void screen_gesture_event_cb(lv_event_t * e)
{
    s_pending_swipe_dir = lv_indev_get_gesture_dir(lv_indev_get_act());
}

/**
 * @brief 应用队列滑动
 * 
 * 该函数负责将滑动操作加入队列，用于处理页面切换。
 * 
 * @param dir - 滑动方向
 */
void app_queue_swipe(lv_dir_t dir)
{
    if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) {
        s_pending_swipe_dir = dir;
    }
}