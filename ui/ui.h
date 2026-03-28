#ifndef UI_H
#define UI_H

#include <math.h>
#include "lvgl.h"
#include "business.h"
#include "ble_beacon.h"

#define LVGL_TICK_PERIOD_MS             2
#define LVGL_TASK_STACK_SIZE            16384
#define LVGL_TASK_PRIORITY              4
#define LVGL_TASK_MAX_DELAY_MS          20
#define LVGL_STARTUP_FRAME_COUNT        3
#define LVGL_STARTUP_FRAME_DELAY_MS     30
#define SCREEN_LOAD_ANIM_TIME_MS        250
#define NAV_BTN_WIDTH                   48
#define NAV_BTN_HEIGHT                  30
#define SETTINGS_ITEM_WIDTH             134
#define SETTINGS_ITEM_HEIGHT            36

// 全局页面变量
extern app_page_t current_page;

// 屏幕对象
extern lv_obj_t *s_home_screen;
extern lv_obj_t *settings_screen;
extern lv_obj_t *health_screen;
extern lv_obj_t *game_screen;
extern lv_obj_t *weather_screen;
extern lv_obj_t *bluetooth_screen;

// 健康监测相关UI元素
extern lv_obj_t *ppg_chart;
extern lv_chart_series_t *ser_hr;
extern lv_chart_series_t *ser_spo2;
extern lv_obj_t *label_hr_val;
extern lv_obj_t *label_spo2_val;
extern lv_timer_t *ppg_timer;

// 游戏相关UI元素
extern lv_obj_t *game_ball;
extern lv_obj_t *game_coin;
extern lv_obj_t *label_score;
extern lv_timer_t *game_timer;

// 天气相关UI元素
extern lv_obj_t *label_city;
extern lv_obj_t *label_temp;
extern lv_obj_t *label_condition;
extern lv_obj_t *label_humidity;
extern lv_obj_t *weather_img;

// 主屏幕相关UI元素
extern lv_obj_t *arc_hr;
extern lv_obj_t *arc_spo2;
extern lv_obj_t *label_weather;
extern lv_obj_t *label_time;
extern lv_obj_t *label_date;
extern lv_obj_t *label_hr_icon;
extern lv_obj_t *label_hr_value;
extern lv_obj_t *label_spo2_icon;
extern lv_obj_t *label_spo2_value;
extern lv_obj_t *hint_label;

// 初始化UI
esp_err_t ui_init(void);

// LVGL任务
void lvgl_task(void *arg);

// LVGL定时器回调
void lvgl_tick_cb(void *arg);

// 创建屏幕
static lv_obj_t *create_screen(lv_color_t bg_color);

// 页面导航
static void load_page(app_page_t page);
static void handle_swipe_dir(lv_dir_t dir);
static void page_nav_event_cb(lv_event_t *e);

// 创建导航按钮
static void create_nav_button(lv_obj_t *parent, const char *text, lv_align_t align, lv_coord_t x_ofs, lv_coord_t y_ofs, app_page_t target_page);
static lv_obj_t *create_menu_item(lv_obj_t *parent, const char *text, lv_coord_t y_ofs, app_page_t target_page);

// 创建各个屏幕
static void create_home_screen(void);
static void create_settings_screen(void);
static void create_health_screen(void);
static void create_game_screen(void);
static void create_weather_screen(void);
static void create_bluetooth_screen(void);

// 定时器回调
static void ppg_timer_cb(lv_timer_t *timer);
static void game_timer_cb(lv_timer_t *timer);

// 事件回调
static void screen_gesture_event_cb(lv_event_t *e);
static void btn_connect_event_cb(lv_event_t *e);
static void btn_disconnect_event_cb(lv_event_t *e);

// 辅助函数
void app_queue_swipe(lv_dir_t dir);
static void warm_up_display(void);

#endif // UI_H