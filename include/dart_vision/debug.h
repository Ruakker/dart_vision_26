#ifndef DART_VISION_DEBUG_H
#define DART_VISION_DEBUG_H

/**
 * 调试输出开关
 *
 * 板端串口控制台带宽很低，且推理线程对时延敏感，因此逐像素/逐帧的调试打印
 * 默认关闭。需要时在编译配置里加 -DDV_DEBUG=1 打开：
 *
 *   cmake -S . -B build -G Ninja -DCMAKE_C_FLAGS=-DDV_DEBUG=1
 *
 * 用法：
 *   #include "dart_vision/debug.h"
 *   DV_DEBUG("Obj count: %d\n", n);
 */

#include <stdio.h>

#ifndef DV_DEBUG
#define DV_DEBUG 0
#endif

#if DV_DEBUG
#define DV_DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DV_DEBUG_PRINT(...) ((void)0)
#endif

#endif // DART_VISION_DEBUG_H
