#include "mpu6500.h"

#include <math.h>
#include <stdbool.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MPU6500_BURST_READ_LEN    14
#define MPU6500_RAD_TO_DEG        57.2957795f
#define MPU6500_POWER_ON_DELAY_MS 50
#define MPU6500_RESET_DELAY_MS    100
#define MPU6500_RETRY_DELAY_MS    50
#define MPU6500_INIT_RETRY_COUNT  3

static const char *TAG = "mpu6500";

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_mpu_dev;
static uint16_t s_mpu_address = MPU6500_I2C_ADDRESS;

static struct {
    bool initialized;
    float pitch;
    float roll;
    int64_t last_update_us;
} s_filter_state;

static esp_err_t mpu6500_write_reg(uint8_t reg_addr, uint8_t value);
static esp_err_t mpu6500_read_regs(uint8_t reg_addr, uint8_t *data, size_t len);
static void mpu6500_convert_raw_to_scaled(const mpu6500_raw_data_t *raw_data,
                                          mpu6500_scaled_data_t *scaled_data);
static esp_err_t mpu6500_select_address(uint16_t device_address);
static esp_err_t mpu6500_configure_device(uint8_t *who_am_i);
static bool mpu6500_is_supported_who_am_i(uint8_t who_am_i);
static const char *mpu6500_describe_who_am_i(uint8_t who_am_i);

/**
 * @brief 初始化I2C总线
 * 
 * 该函数负责初始化I2C总线，包括：
 * 1. 检查是否已初始化
 * 2. 创建或获取现有的I2C总线
 * 3. 选择MPU6500的I2C地址
 * 
 * @return esp_err_t - 操作结果，成功返回ESP_OK
 */
esp_err_t i2c_master_init(void)
{
    esp_err_t ret;

    if (s_i2c_bus != NULL && s_mpu_dev != NULL) {
        return ESP_OK;
    }

    if (s_i2c_bus == NULL) {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = MPU6500_I2C_PORT,
            .sda_io_num = MPU6500_I2C_SDA_GPIO,
            .scl_io_num = MPU6500_I2C_SCL_GPIO,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags.enable_internal_pullup = 1,
        };

        ret = i2c_new_master_bus(&bus_config, &s_i2c_bus);
        if (ret == ESP_ERR_INVALID_STATE) {
            ret = i2c_master_get_bus_handle(MPU6500_I2C_PORT, &s_i2c_bus);
            ESP_RETURN_ON_ERROR(ret, TAG, "get existing I2C bus failed");
            ESP_LOGI(TAG, "reuse existing I2C bus on port %d", MPU6500_I2C_PORT);
        } else {
            ESP_RETURN_ON_ERROR(ret, TAG, "create I2C bus failed");
            ESP_LOGI(TAG, "create I2C bus on GPIO%d/GPIO%d", MPU6500_I2C_SDA_GPIO, MPU6500_I2C_SCL_GPIO);
        }
    }

    return mpu6500_select_address(s_mpu_address);
}

/**
 * @brief 初始化MPU6500传感器
 * 
 * 该函数负责初始化MPU6500传感器，包括：
 * 1. 初始化I2C总线
 * 2. 尝试不同的I2C地址
 * 3. 配置设备
 * 4. 检查WHO_AM_I寄存器
 * 5. 重置姿态滤波器
 * 
 * @return esp_err_t - 操作结果，成功返回ESP_OK
 */
esp_err_t mpu6500_init(void)
{
    static const uint16_t candidate_addresses[] = {
        MPU6500_I2C_ADDRESS,
        MPU6500_I2C_ADDRESS_ALT,
    };
    uint8_t who_am_i = 0;
    esp_err_t ret = ESP_FAIL;

    ESP_RETURN_ON_ERROR(i2c_master_init(), TAG, "I2C init failed");

    vTaskDelay(pdMS_TO_TICKS(MPU6500_POWER_ON_DELAY_MS));

    for (int attempt = 1; attempt <= MPU6500_INIT_RETRY_COUNT; attempt++) {
        for (size_t i = 0; i < sizeof(candidate_addresses) / sizeof(candidate_addresses[0]); i++) {
            const uint16_t address = candidate_addresses[i];

            ret = mpu6500_select_address(address);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "attach I2C address 0x%02X failed: %s", address, esp_err_to_name(ret));
                continue;
            }

            ret = mpu6500_configure_device(&who_am_i);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG,
                         "MPU init attempt %d/%d at 0x%02X failed: %s",
                         attempt,
                         MPU6500_INIT_RETRY_COUNT,
                         address,
                         esp_err_to_name(ret));
                continue;
            }

            if (!mpu6500_is_supported_who_am_i(who_am_i)) {
                ESP_LOGW(TAG,
                         "Device at 0x%02X returned unexpected WHO_AM_I=0x%02X (%s)",
                         address,
                         who_am_i,
                         mpu6500_describe_who_am_i(who_am_i));
                ret = ESP_ERR_NOT_FOUND;
                continue;
            }

            ESP_LOGI(TAG,
                     "Detected %s at 0x%02X, WHO_AM_I=0x%02X",
                     mpu6500_describe_who_am_i(who_am_i),
                     address,
                     who_am_i);

            mpu6500_reset_attitude_filter();
            return ESP_OK;
        }

        if (attempt < MPU6500_INIT_RETRY_COUNT) {
            vTaskDelay(pdMS_TO_TICKS(MPU6500_RETRY_DELAY_MS));
        }
    }

    return ret;
}

/**
 * @brief 读取MPU6500的WHO_AM_I寄存器
 * 
 * 该函数负责读取MPU6500传感器的WHO_AM_I寄存器，用于识别设备类型。
 * 
 * @param who_am_i - 指向存储WHO_AM_I值的指针
 * @return esp_err_t - 操作结果，成功返回ESP_OK
 */
esp_err_t mpu6500_read_who_am_i(uint8_t *who_am_i)
{
    ESP_RETURN_ON_FALSE(who_am_i != NULL, ESP_ERR_INVALID_ARG, TAG, "who_am_i is NULL");
    return mpu6500_read_regs(MPU6500_REG_WHO_AM_I, who_am_i, 1);
}

/**
 * @brief 读取MPU6500的原始数据
 * 
 * 该函数负责读取MPU6500传感器的原始数据，包括加速度计、陀螺仪和温度数据。
 * 
 * @param raw_data - 指向存储原始数据的结构体指针
 * @return esp_err_t - 操作结果，成功返回ESP_OK
 */
esp_err_t mpu6500_read_raw(mpu6500_raw_data_t *raw_data)
{
    uint8_t data[MPU6500_BURST_READ_LEN] = {0};

    ESP_RETURN_ON_FALSE(raw_data != NULL, ESP_ERR_INVALID_ARG, TAG, "raw_data is NULL");
    ESP_RETURN_ON_ERROR(mpu6500_read_regs(MPU6500_REG_ACCEL_XOUT_H, data, sizeof(data)), TAG, "burst read failed");

    raw_data->accel_x = (int16_t)((data[0] << 8) | data[1]);
    raw_data->accel_y = (int16_t)((data[2] << 8) | data[3]);
    raw_data->accel_z = (int16_t)((data[4] << 8) | data[5]);
    raw_data->temp = (int16_t)((data[6] << 8) | data[7]);
    raw_data->gyro_x = (int16_t)((data[8] << 8) | data[9]);
    raw_data->gyro_y = (int16_t)((data[10] << 8) | data[11]);
    raw_data->gyro_z = (int16_t)((data[12] << 8) | data[13]);

    return ESP_OK;
}

/**
 * @brief 读取MPU6500的缩放数据
 * 
 * 该函数负责读取MPU6500传感器的缩放数据，包括加速度计（单位：g）、陀螺仪（单位：°/s）和温度（单位：°C）。
 * 
 * @param scaled_data - 指向存储缩放数据的结构体指针
 * @return esp_err_t - 操作结果，成功返回ESP_OK
 */
esp_err_t mpu6500_read_scaled(mpu6500_scaled_data_t *scaled_data)
{
    mpu6500_raw_data_t raw_data;

    ESP_RETURN_ON_FALSE(scaled_data != NULL, ESP_ERR_INVALID_ARG, TAG, "scaled_data is NULL");
    ESP_RETURN_ON_ERROR(mpu6500_read_raw(&raw_data), TAG, "read raw data failed");

    mpu6500_convert_raw_to_scaled(&raw_data, scaled_data);
    return ESP_OK;
}

/**
 * @brief 读取MPU6500的姿态数据
 * 
 * 该函数负责读取MPU6500传感器的姿态数据，包括俯仰角和滚转角，使用互补滤波器进行数据融合。
 * 
 * @param attitude - 指向存储姿态数据的结构体指针
 * @return esp_err_t - 操作结果，成功返回ESP_OK
 */
esp_err_t mpu6500_read_attitude(mpu6500_attitude_t *attitude)
{
    int64_t now_us;
    float dt_s;
    float pitch_acc;
    float roll_acc;
    mpu6500_scaled_data_t scaled_data;

    ESP_RETURN_ON_FALSE(attitude != NULL, ESP_ERR_INVALID_ARG, TAG, "attitude is NULL");
    ESP_RETURN_ON_ERROR(mpu6500_read_scaled(&scaled_data), TAG, "read scaled data failed");

    pitch_acc = atan2f(-scaled_data.accel_x_g,
                       sqrtf((scaled_data.accel_y_g * scaled_data.accel_y_g) +
                             (scaled_data.accel_z_g * scaled_data.accel_z_g))) * MPU6500_RAD_TO_DEG;
    roll_acc = atan2f(scaled_data.accel_y_g, scaled_data.accel_z_g) * MPU6500_RAD_TO_DEG;

    now_us = esp_timer_get_time();
    if (!s_filter_state.initialized) {
        s_filter_state.pitch = pitch_acc;
        s_filter_state.roll = roll_acc;
        s_filter_state.last_update_us = now_us;
        s_filter_state.initialized = true;
    } else {
        dt_s = (float)(now_us - s_filter_state.last_update_us) / 1000000.0f;
        if (dt_s <= 0.0f || dt_s > 0.2f) {
            dt_s = 0.01f;
        }

        s_filter_state.pitch = (MPU6500_COMPLEMENTARY_ALPHA *
                                (s_filter_state.pitch + (scaled_data.gyro_y_dps * dt_s))) +
                               ((1.0f - MPU6500_COMPLEMENTARY_ALPHA) * pitch_acc);
        s_filter_state.roll = (MPU6500_COMPLEMENTARY_ALPHA *
                               (s_filter_state.roll + (scaled_data.gyro_x_dps * dt_s))) +
                              ((1.0f - MPU6500_COMPLEMENTARY_ALPHA) * roll_acc);
        s_filter_state.last_update_us = now_us;
    }

    attitude->pitch = s_filter_state.pitch;
    attitude->roll = s_filter_state.roll;
    return ESP_OK;
}

/**
 * @brief 重置MPU6500的姿态滤波器
 * 
 * 该函数负责重置MPU6500传感器的姿态滤波器，将滤波器状态初始化。
 */
void mpu6500_reset_attitude_filter(void)
{
    s_filter_state.initialized = false;
    s_filter_state.pitch = 0.0f;
    s_filter_state.roll = 0.0f;
    s_filter_state.last_update_us = 0;
}

static esp_err_t mpu6500_write_reg(uint8_t reg_addr, uint8_t value)
{
    uint8_t write_buf[2] = {reg_addr, value};

    ESP_RETURN_ON_FALSE(s_mpu_dev != NULL, ESP_ERR_INVALID_STATE, TAG, "MPU6500 not initialized");
    return i2c_master_transmit(s_mpu_dev, write_buf, sizeof(write_buf), -1);
}

static esp_err_t mpu6500_read_regs(uint8_t reg_addr, uint8_t *data, size_t len)
{
    ESP_RETURN_ON_FALSE(data != NULL, ESP_ERR_INVALID_ARG, TAG, "data is NULL");
    ESP_RETURN_ON_FALSE(s_mpu_dev != NULL, ESP_ERR_INVALID_STATE, TAG, "MPU6500 not initialized");

    return i2c_master_transmit_receive(s_mpu_dev, &reg_addr, 1, data, len, -1);
}

static void mpu6500_convert_raw_to_scaled(const mpu6500_raw_data_t *raw_data,
                                          mpu6500_scaled_data_t *scaled_data)
{
    scaled_data->accel_x_g = raw_data->accel_x / MPU6500_ACCEL_SENSITIVITY_2G;
    scaled_data->accel_y_g = raw_data->accel_y / MPU6500_ACCEL_SENSITIVITY_2G;
    scaled_data->accel_z_g = raw_data->accel_z / MPU6500_ACCEL_SENSITIVITY_2G;

    scaled_data->gyro_x_dps = raw_data->gyro_x / MPU6500_GYRO_SENSITIVITY_250DPS;
    scaled_data->gyro_y_dps = raw_data->gyro_y / MPU6500_GYRO_SENSITIVITY_250DPS;
    scaled_data->gyro_z_dps = raw_data->gyro_z / MPU6500_GYRO_SENSITIVITY_250DPS;

    scaled_data->temperature_c = (raw_data->temp / MPU6500_TEMP_SENSITIVITY) + MPU6500_TEMP_OFFSET_C;
}

static esp_err_t mpu6500_select_address(uint16_t device_address)
{
    esp_err_t ret;

    ESP_RETURN_ON_FALSE(s_i2c_bus != NULL, ESP_ERR_INVALID_STATE, TAG, "I2C bus not initialized");

    if (s_mpu_dev != NULL && s_mpu_address == device_address) {
        return ESP_OK;
    }

    if (s_mpu_dev != NULL) {
        ESP_RETURN_ON_ERROR(i2c_master_bus_rm_device(s_mpu_dev), TAG, "remove old I2C device failed");
        s_mpu_dev = NULL;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = device_address,
        .scl_speed_hz = MPU6500_I2C_FREQ_HZ,
        .scl_wait_us = 0,
    };

    ret = i2c_master_bus_add_device(s_i2c_bus, &dev_config, &s_mpu_dev);
    ESP_RETURN_ON_ERROR(ret, TAG, "add MPU device to I2C bus failed");

    s_mpu_address = device_address;
    ESP_LOGI(TAG, "using I2C address 0x%02X on GPIO%d/GPIO%d", device_address, MPU6500_I2C_SDA_GPIO, MPU6500_I2C_SCL_GPIO);
    return ESP_OK;
}

static esp_err_t mpu6500_configure_device(uint8_t *who_am_i)
{
    ESP_RETURN_ON_FALSE(who_am_i != NULL, ESP_ERR_INVALID_ARG, TAG, "who_am_i is NULL");

    ESP_RETURN_ON_ERROR(mpu6500_write_reg(MPU6500_REG_PWR_MGMT_1, 0x80), TAG, "device reset failed");
    vTaskDelay(pdMS_TO_TICKS(MPU6500_RESET_DELAY_MS));

    ESP_RETURN_ON_ERROR(mpu6500_write_reg(MPU6500_REG_PWR_MGMT_1, 0x01), TAG, "wake up failed");
    ESP_RETURN_ON_ERROR(mpu6500_write_reg(MPU6500_REG_PWR_MGMT_2, 0x00), TAG, "enable sensors failed");
    ESP_RETURN_ON_ERROR(mpu6500_write_reg(MPU6500_REG_SMPLRT_DIV, 0x04), TAG, "set sample rate failed");
    ESP_RETURN_ON_ERROR(mpu6500_write_reg(MPU6500_REG_CONFIG, 0x03), TAG, "set dlpf failed");
    ESP_RETURN_ON_ERROR(mpu6500_write_reg(MPU6500_REG_GYRO_CONFIG, 0x00), TAG, "set gyro range failed");
    ESP_RETURN_ON_ERROR(mpu6500_write_reg(MPU6500_REG_ACCEL_CONFIG, 0x00), TAG, "set accel range failed");
    ESP_RETURN_ON_ERROR(mpu6500_write_reg(MPU6500_REG_ACCEL_CONFIG_2, 0x03), TAG, "set accel dlpf failed");
    ESP_RETURN_ON_ERROR(mpu6500_write_reg(MPU6500_REG_INT_ENABLE, 0x00), TAG, "disable interrupt failed");

    return mpu6500_read_who_am_i(who_am_i);
}

static bool mpu6500_is_supported_who_am_i(uint8_t who_am_i)
{
    return (who_am_i == 0x68) || (who_am_i == 0x70) || (who_am_i == 0x71);
}

static const char *mpu6500_describe_who_am_i(uint8_t who_am_i)
{
    switch (who_am_i) {
        case 0x68:
            return "MPU6050-compatible";
        case 0x70:
            return "MPU6500";
        case 0x71:
            return "MPU9250-compatible";
        case 0x00:
            return "all-zero response";
        default:
            return "unknown device";
    }
}
