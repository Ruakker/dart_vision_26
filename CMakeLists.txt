cmake_minimum_required(VERSION 3.15)
project(dart_vision)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_COMPILER "$ENV{COMPILER}/riscv64-unknown-linux-musl-gcc")
set(CMAKE_CXX_COMPILER "$ENV{COMPILER}/riscv64-unknown-linux-musl-g++")

set(CVI_SDK_ROOT "${CMAKE_SOURCE_DIR}/sdk")
set(CVI_TDL_SDK_DIR "${CVI_SDK_ROOT}/tdl_sdk")
set(CVI_MW_DIR "${CVI_SDK_ROOT}/middleware")
set(CVI_OSDRV_DIR "${CVI_SDK_ROOT}/osdrv")

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)
file(MAKE_DIRECTORY ${EXECUTABLE_OUTPUT_PATH})

add_library(common_settings INTERFACE)

target_compile_options(common_settings INTERFACE
    -D__CV181X__ -O3 -march=rv64imafd -DSENSOR_GCORE_GC4653
    -D_MIDDLEWARE_V2_ -DC906 -DUSE_TPU_IVE -fsigned-char
    -Wno-format-truncation -fdiagnostics-color=always -s
)

target_include_directories(common_settings INTERFACE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CVI_TDL_SDK_DIR}/include
    ${CVI_TDL_SDK_DIR}/sample
    ${CVI_TDL_SDK_DIR}/include/core/utils
    ${CVI_MW_DIR}/include
    ${CVI_MW_DIR}/modules/isp/include/cv181x
    ${CVI_MW_DIR}/component/isp/common
    ${CVI_MW_DIR}/sample/common
    ${CVI_MW_DIR}/v2/sample/common
    ${CVI_MW_DIR}/v2/include
    ${CVI_MW_DIR}/v2/modules/isp/include/cv181x/
    ${CVI_MW_DIR}/modules/ive/include
    ${CVI_MW_DIR}/3rdparty/
    ${CVI_OSDRV_DIR}/interdrv/v2/include/chip/cv181x/uapi
    ${CVI_OSDRV_DIR}/interdrv/include/common/uapi
    ${CVI_SDK_ROOT}/cvi_rtsp/include/cvi_rtsp
    ${CVI_SDK_ROOT}/opencv/include
)

target_link_directories(common_settings INTERFACE
    ${CVI_SDK_ROOT}/prebuilt/lib
    ${CVI_SDK_ROOT}/prebuilt/usr/lib
    ${CVI_SDK_ROOT}/prebuilt/usr/lib/3rd
)

target_link_libraries(common_settings INTERFACE
    -lcvi_tdl -lcvi_tdl_app -lsample -lcvi_rtsp -lcvikernel -lcvimath -lcviruntime
    -lz -lm -lcvi_ive -lopencv_core -lopencv_imgproc -lopencv_imgcodecs
    -lsys -lvpss -lcvi_bin -lvi -lvo -lgdc -lrgn -lsns_full -lisp -lvenc -lvdec
    -lawb -lae -laf -lcvi_bin_isp -lini -lmisc -lisp_algo -lpthread -latomic
)

set(DART_VISION_SRC
    src/main.c
    src/config.c
    src/uart.c
    src/tdl.c
    src/venc.c
    src/osd_rgn.c
    src/kalman.c
    src/cJSON.c
    src/serial/drv_cmd_com.c
    src/serial/drv_CRC.c
    src/middleware_utils.c
    src/sample_utils.c
)

add_executable(dart_vision ${DART_VISION_SRC})
target_link_libraries(dart_vision PRIVATE common_settings)

set_target_properties(dart_vision PROPERTIES
    BUILD_RPATH_USE_ORIGIN TRUE
    INSTALL_RPATH "/mnt/system/lib:/mnt/system/usr/lib:/mnt/system/usr/lib/3rd"
    BUILD_RPATH "/mnt/system/lib:/mnt/system/usr/lib:/mnt/system/usr/lib/3rd"
)
