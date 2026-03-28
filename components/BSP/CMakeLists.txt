set(src_dirs
          Api
           ble_beacon
           max30102
           mpu6500
           KEY
           LED
           GC9A01
           CST816S)

set(include_dirs
            Api
            ble_beacon
            max30102
            mpu6500
            KEY
            LED
            GC9A01
            CST816S)

set(requires
            esp_driver_gpio
            esp_driver_i2c
            esp_driver_spi
			esp_http_client
            freertos
			json
			mbedtls
            bt           
            nvs_flash)  

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires} lvgl)
component_compile_options(-ffast-math -O3 -Wno-error=format -Wno-format)
