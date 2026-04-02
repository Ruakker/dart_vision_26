#ifndef DART_VISION_TDL_H_
#define DART_VISION_TDL_H_

// TDL 推理线程接口
#include "cvi_comm.h"
#include "cvi_tdl.h"
#include "dart_vision/state.h"

// TDL 线程参数：包含 TDL 句柄与共享状态
typedef struct {
  cvitdl_handle_t tdl_handle;
  State *state;
} TdlThreadArgs;

CVI_S32 init_tdl_param(cvitdl_handle_t tdl_handle);
void *run_tdl(void *args);

// 从检测列表中选取得分最高且长宽比合理的目标
bool select_best_object(const VIDEO_FRAME_INFO_S *frame, const cvtdl_object_t *meta,
                        const State *state, float dt, float aspect_ratio_thresh,
                        cvtdl_object_info_t **best);

#endif
