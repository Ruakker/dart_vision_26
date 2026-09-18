#ifndef DART_VISION_VENC_H_
#define DART_VISION_VENC_H_

// 编码/绘制线程接口
#include "middleware_utils.h"
#include "cvi_tdl.h"
#include "dart_vision/record_writer.h"
#include "dart_vision/state.h"

// 编码/绘制线程参数：中间件上下文 + 绘制服务 + 共享状态
typedef struct {
  SAMPLE_TDL_MW_CONTEXT *mw_context;
  cvitdl_service_handle_t service_handle;
  State *state;
  RecordWriter *record_writer;
} VencThreadArgs;

void *run_venc(void *args);

#endif
