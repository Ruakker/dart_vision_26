cmake_minimum_required(VERSION 3.15)

# ---------------------------------------------------------------------------
# 交叉编译工具链：必须在 project() 之前设置，否则 CMake 会先探测到主机编译器
# （/usr/bin/cc）并缓存下来，导致编不出 RISC-V 目标文件。
# ---------------------------------------------------------------------------
if(NOT DEFINED ENV{COMPILER} OR "$ENV{COMPILER}" STREQUAL "")
  message(FATAL_ERROR
    "环境变量 COMPILER 未设置。\n"
    "请指向包含 riscv64-unknown-linux-musl-gcc / g++ 的目录，例如：\n"
    "  export COMPILER=/opt/riscv64-linux-musl/bin\n"
    "  cmake -S . -B build -G Ninja")
endif()
if(NOT EXISTS "$ENV{COMPILER}/riscv64-unknown-linux-musl-gcc")
  message(FATAL_ERROR
    "在 $ENV{COMPILER} 下找不到 riscv64-unknown-linux-musl-gcc。\n"
    "COMPILER 必须是工具链的 bin 目录（其中同时包含 riscv64-unknown-linux-musl-gcc 与 g++）。")
endif()

set(CMAKE_C_COMPILER "$ENV{COMPILER}/riscv64-unknown-linux-musl-gcc")
set(CMAKE_CXX_COMPILER "$ENV{COMPILER}/riscv64-unknown-linux-musl-g++")
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(dart_vision C CXX)

# ---------------------------------------------------------------------------
# 平台 SDK：体积大、厂商版权，不随仓库分发，需先运行 scripts/fetch_sdk.sh
# 详见 README.md 的「平台 SDK」一节
# ---------------------------------------------------------------------------
set(CVI_SDK_ROOT "${CMAKE_SOURCE_DIR}/sdk")
if(NOT EXISTS "${CVI_SDK_ROOT}/tdl_sdk/include/cvi_tdl.h")
  message(FATAL_ERROR
    "找不到平台 SDK：${CVI_SDK_ROOT}/tdl_sdk/include/cvi_tdl.h\n"
    "SDK 不随仓库分发，请先执行：\n"
    "  ./scripts/fetch_sdk.sh --from-dir /path/to/your/sdk\n"
    "  ./scripts/fetch_sdk.sh --check\n"
    "说明见 README.md 的「平台 SDK」一节")
endif()

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
    src/record_writer.c
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
