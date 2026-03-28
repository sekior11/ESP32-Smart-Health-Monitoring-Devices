# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "E:/esp/Espressif/frameworks/esp-idf-v5.4/components/bootloader/subproject"
  "C:/Users/27846/Desktop/esp32project/LVGL_test_2/build_codex/bootloader"
  "C:/Users/27846/Desktop/esp32project/LVGL_test_2/build_codex/bootloader-prefix"
  "C:/Users/27846/Desktop/esp32project/LVGL_test_2/build_codex/bootloader-prefix/tmp"
  "C:/Users/27846/Desktop/esp32project/LVGL_test_2/build_codex/bootloader-prefix/src/bootloader-stamp"
  "C:/Users/27846/Desktop/esp32project/LVGL_test_2/build_codex/bootloader-prefix/src"
  "C:/Users/27846/Desktop/esp32project/LVGL_test_2/build_codex/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Users/27846/Desktop/esp32project/LVGL_test_2/build_codex/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/Users/27846/Desktop/esp32project/LVGL_test_2/build_codex/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
