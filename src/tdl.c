// 推理线程：取帧 -> 推理 -> 追踪 -> 缓存检测结果
#include "../include/dart_vision/tdl.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <core/utils/vpss_helper.h>

#include "../include/dart_vision/config.h"
#include "../include/dart_vision/kalman.h"
#include "../include/dart_vision/uart.h"
#include "core/core/cvtdl_core_types.h"

// -------------------- 数学常量 --------------------
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// 限幅工具
static float clamp_float(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static inline float compensated_to_raw_yaw(const State *state, float yaw_deg) {
  return yaw_deg + (state ? state->optical_axis_yaw_comp_deg : 0.0f);
}

static inline float compensated_to_raw_pitch(const State *state, float pitch_deg) {
  return pitch_deg + (state ? state->optical_axis_pitch_comp_deg : 0.0f);
}

// 角度(度) -> 像素坐标
static void angle_to_pixel(const CameraParams *camera, float yaw_deg, float pitch_deg, float *px,
                           float *py) {
  if (!camera || !px || !py || camera->fx <= 0.0f || camera->fy <= 0.0f) {
    if (px) *px = 0.0f;
    if (py) *py = 0.0f;
    return;
  }

  float yaw_rad = yaw_deg * (float)M_PI / 180.0f;
  float pitch_rad = pitch_deg * (float)M_PI / 180.0f;
  // 相机旋转 90°：yaw→垂直(y, 上正), pitch→水平(x, 右正)
  *px = tanf(pitch_rad) * camera->fx + camera->cx;
  *py = -tanf(yaw_rad) * camera->fy + camera->cy;
}

// 从卡尔曼状态预测当前像素位置（返回推理图坐标）
static bool predict_pixel_from_kf(const State *state, float dt, float *px, float *py) {
  if (!state || !px || !py) {
    return false;
  }
  if (!state->kf.initialized) {
    return false;
  }
  if (state->camera.fx <= 0.0f || state->camera.fy <= 0.0f) {
    return false;
  }

  float pred_yaw = state->kf.x[0] + dt * state->kf.x[1];
  float pred_pitch = state->kf.x[2] + dt * state->kf.x[3];
  float raw_pred_yaw = compensated_to_raw_yaw(state, pred_yaw);
  float raw_pred_pitch = compensated_to_raw_pitch(state, pred_pitch);

  float orig_x = 0.0f;
  float orig_y = 0.0f;
  angle_to_pixel(&state->camera, raw_pred_yaw, raw_pred_pitch, &orig_x, &orig_y);

  // 传感器坐标 → 裁剪区域坐标
  float crop_x = orig_x - (float)state->crop_x;
  float crop_y = orig_y - (float)state->crop_y;

  // 裁剪区域坐标 → 旋转后坐标（VPSS 90° CW）
  float S = (float)state->crop_size;
  float rot_x = S - 1.0f - crop_y;
  float rot_y = crop_x;

  // 旋转后坐标 → 推理图坐标（缩放到 320×320）
  if (state->crop_size > 0 && state->crop_scale > 1e-6f) {
    *px = rot_x / state->crop_scale;
    *py = rot_y / state->crop_scale;
  } else {
    *px = rot_x;
    *py = rot_y;
  }
  return true;
}

// 角速度(度/秒) -> 像素速度(像素/秒)
static void angle_velocity_to_pixel_velocity(const CameraParams *camera, float yaw_deg,
                                             float pitch_deg, float v_yaw_deg, float v_pitch_deg,
                                             float *v_px, float *v_py) {
  if (!camera || !v_px || !v_py || camera->fx <= 0.0f || camera->fy <= 0.0f) {
    if (v_px) *v_px = 0.0f;
    if (v_py) *v_py = 0.0f;
    return;
  }

  float yaw_rad = yaw_deg * (float)M_PI / 180.0f;
  float pitch_rad = pitch_deg * (float)M_PI / 180.0f;
  float cos_yaw = cosf(yaw_rad);
  float cos_pitch = cosf(pitch_rad);
  float dyaw = v_yaw_deg * (float)M_PI / 180.0f;
  float dpitch = v_pitch_deg * (float)M_PI / 180.0f;
  // 相机旋转 90°：pitch速度→水平(x), yaw速度→垂直(y, 上正)
  float dx_dpitch = camera->fx / (cos_pitch * cos_pitch);
  float dy_dyaw = camera->fy / (cos_yaw * cos_yaw);
  *v_px = dx_dpitch * dpitch;
  *v_py = -dy_dyaw * dyaw;
}

#include <stdbool.h>
#include <stdint.h>
#include "cvi_vpss.h"

// 辅助函数：判断单个点是否满足绿色特征
// NV12/NV21 格式通用判断 (只要 U 和 V 都小，顺序不重要)
static inline bool is_pixel_green(uint8_t *uv_plane, uint32_t stride, int x, int y) {
  // NV12/21 UV平面高度减半，x坐标对应2个字节
  // offset = (y / 2) * stride + (x / 2) * 2
  uint32_t offset = (y >> 1) * stride + (x & ~1);

  uint8_t val_1 = uv_plane[offset];
  uint8_t val_2 = uv_plane[offset + 1];
  printf("val_1: %d, val_2: %d\n", val_1, val_2);

  // 两个分量都必须小于阈值
  return (val_1 < GREEN_UV_THRESH && val_2 < GREEN_UV_THRESH);
}

// 辅助函数：映射物理地址到虚拟地址
void map_frame_virtual_addr(VIDEO_FRAME_INFO_S *frame) {
    // 如果已经在其他地方映射过了，就跳过
    if (frame->stVFrame.pu8VirAddr[0] != NULL) return;

    // 1. 映射 Y 平面 (Plane 0)
    // 大小 = 步长 * 高度
    uint32_t size_y = frame->stVFrame.u32Stride[0] * frame->stVFrame.u32Height;
    frame->stVFrame.pu8VirAddr[0] = (uint8_t *)CVI_SYS_Mmap(frame->stVFrame.u64PhyAddr[0], size_y);

    // 2. 映射 UV 平面 (Plane 1)
    // NV12/NV21 格式下，UV平面高度减半，但每像素2字节，总体积是 Y 的一半
    // 大小 = 步长 * (高度 / 2)
    uint32_t size_uv = frame->stVFrame.u32Stride[1] * (frame->stVFrame.u32Height / 2);
    frame->stVFrame.pu8VirAddr[1] = (uint8_t *)CVI_SYS_Mmap(frame->stVFrame.u64PhyAddr[1], size_uv);

    // *重要*：在读取之前，最好做一次 Cache 失效操作，确保 CPU 读到的是内存里最新的数据
    // 如果发现图像有条纹或脏数据，请取消下面这行的注释
    // CVI_SYS_IonInvalidateCache(frame->stVFrame.u64PhyAddr[0], frame->stVFrame.pu8VirAddr[0], size_y + size_uv);
}

// 辅助函数：解除映射 (防止内存泄漏)
void unmap_frame_virtual_addr(VIDEO_FRAME_INFO_S *frame) {
    // 1. 解除 Y 平面
    if (frame->stVFrame.pu8VirAddr[0]) {
        uint32_t size_y = frame->stVFrame.u32Stride[0] * frame->stVFrame.u32Height;
        CVI_SYS_Munmap((void *)frame->stVFrame.pu8VirAddr[0], size_y);
        frame->stVFrame.pu8VirAddr[0] = NULL;
    }

    // 2. 解除 UV 平面
    if (frame->stVFrame.pu8VirAddr[1]) {
        uint32_t size_uv = frame->stVFrame.u32Stride[1] * (frame->stVFrame.u32Height / 2);
        CVI_SYS_Munmap((void *)frame->stVFrame.pu8VirAddr[1], size_uv);
        frame->stVFrame.pu8VirAddr[1] = NULL;
    }
}

static bool check_object_is_green(const VIDEO_FRAME_INFO_S *frame, const cvtdl_bbox_t *bbox) {
    // 1. 获取 UV 平面地址
    if (!frame->stVFrame.pu8VirAddr[1]) return false; // 无法判断，默认丢弃

    uint8_t *uv_plane = (uint8_t *)frame->stVFrame.pu8VirAddr[1];
    uint32_t stride = frame->stVFrame.u32Stride[1];
    uint32_t width = frame->stVFrame.u32Width;
    uint32_t height = frame->stVFrame.u32Height;

    // 2. 坐标边界限制
    int x1 = (int)bbox->x1 < 0 ? 0 : (int)bbox->x1;
    int y1 = (int)bbox->y1 < 0 ? 0 : (int)bbox->y1;
    int x2 = (int)bbox->x2 >= width ? width - 1 : (int)bbox->x2;
    int y2 = (int)bbox->y2 >= height ? height - 1 : (int)bbox->y2;

    // 框太小，直接退化为取中心点
    if ((x2 - x1) < 4 || (y2 - y1) < 4) {
        return is_pixel_green(uv_plane, stride, (x1+x2)/2, (y1+y2)/2);
    }

    int cx = (x1 + x2) / 2; // 中心 X
    int cy = (y1 + y2) / 2; // 中心 Y

    // 3. 扫描策略：十字交叉
    // 步长 step = 2 (因为 NV12 每 2 像素共用 UV，逐像素扫描没意义且慢)
    int step = 2; 

    // A. 横向扫描 (沿着中心水平线)：从左到右
    for (int x = x1; x <= x2; x += step) {
        if (is_pixel_green(uv_plane, stride, x, cy)) {
            return true;
        }
    }

    // B. 纵向扫描 (沿着中心垂直线)：从上到下
    for (int y = y1; y <= y2; y += step) {
        if (is_pixel_green(uv_plane, stride, cx, y)) {
            return true;
        }
    }

    // 4. 如果十字线上全都没找到绿色（全是白色/过曝/背景），则丢弃
    return false;
}

// 从检测列表中选取得分最高且长宽比合理的目标
bool select_best_object(const VIDEO_FRAME_INFO_S *frame, const cvtdl_object_t *meta,
                        const State *state, float dt, float aspect_ratio_thresh,
                        cvtdl_object_info_t **best) {
  if (!frame || !meta || meta->size <= 0 || !meta->info) {
    return false;
  }

  float pred_x = 0.0f;
  float pred_y = 0.0f;
  bool has_pred = predict_pixel_from_kf(state, dt, &pred_x, &pred_y);
  float conf_weight = 0.5f;
  if (state) {
    conf_weight = clamp_float(state->kf_cfg.confidence_weight, 0.0f, 1.0f);
  }
  float dist_weight = 1.0f - conf_weight;
  float max_dist = sqrtf((float)frame->stVFrame.u32Width * (float)frame->stVFrame.u32Width +
                         (float)frame->stVFrame.u32Height * (float)frame->stVFrame.u32Height);

  int best_idx = -1;
  float best_score = -1.0f;
  for (int i = 0; i < meta->size; i++) {
    float w = meta->info[i].bbox.x2 - meta->info[i].bbox.x1;
    float h = meta->info[i].bbox.y2 - meta->info[i].bbox.y1;
    if (w <= 0.0f || h <= 0.0f) {
      continue;
    }
    float ratio = w > h ? (w / h) : (h / w);
    if (ratio > aspect_ratio_thresh) {
      // printf("Object %d filtered out by aspect ratio: %f\n", i, ratio);
      continue;
    }
    if (!check_object_is_green(frame, &meta->info[i].bbox)) {
      // printf("Object %d filtered out as non-green\n", i);
      continue;
    }
    float score = meta->info[i].bbox.score;
    if (has_pred && max_dist > 1e-6f) {
      float center_x = (meta->info[i].bbox.x1 + meta->info[i].bbox.x2) * 0.5f;
      float center_y = (meta->info[i].bbox.y1 + meta->info[i].bbox.y2) * 0.5f;
      float dx = center_x - pred_x;
      float dy = center_y - pred_y;
      float dist = sqrtf(dx * dx + dy * dy);
      float dist_norm = clamp_float(dist / max_dist, 0.0f, 1.0f);
      float dist_score = 1.0f - dist_norm;
      score = conf_weight * score + dist_weight * dist_score;
    }
    if (score > best_score) {
      best_score = score;
      best_idx = i;
    }
  }

  if (best_idx < 0) {
    return false;
  }

  if (best) {
    *best = &meta->info[best_idx];
    // printf("Selected best object %d with score %f\n", best_idx, best_score);
  }
  return true;
}

// 计算相邻帧时间间隔并裁剪到配置范围
static float calc_delta_time(const KalmanConfig *cfg, struct timespec *last_ts, bool *has_last_ts) {
  struct timespec now_ts;
  clock_gettime(CLOCK_MONOTONIC, &now_ts);
  float dt = cfg->dt_default;
  if (*has_last_ts) {
    double delta =
        (now_ts.tv_sec - last_ts->tv_sec) + (now_ts.tv_nsec - last_ts->tv_nsec) / 1000000000.0;
    dt = (float)delta;
  }
  *has_last_ts = true;
  *last_ts = now_ts;
  return clamp_float(dt, cfg->dt_min, cfg->dt_max);
}

// 计算最佳目标的测量角度（度）
static bool measure_best_angle(const VIDEO_FRAME_INFO_S *frame, const cvtdl_object_t *meta,
                               const State *state, float dt, float aspect_ratio_thresh,
                               float *yaw_deg, float *pitch_deg, float *distance_m,
                               bool *distance_valid) {
  if (!frame || !meta || !state || !yaw_deg || !pitch_deg || !distance_m || !distance_valid) {
    return false;
  }

  cvtdl_object_info_t *obj = NULL;
  if (!select_best_object(frame, meta, state, dt, aspect_ratio_thresh, &obj) || !obj) {
    return false;
  }

  float center_x = (obj->bbox.x1 + obj->bbox.x2) * 0.5f;
  float center_y = (obj->bbox.y1 + obj->bbox.y2) * 0.5f;
  float bbox_h = obj->bbox.y2 - obj->bbox.y1;
  float measure_x = center_x;
  float measure_y = center_y - TARGET_UP_OFFSET_HEIGHT_FACTOR * bbox_h;

  measure_x = clamp_float(measure_x, 0.0f, (float)(frame->stVFrame.u32Width - 1));
  measure_y = clamp_float(measure_y, 0.0f, (float)(frame->stVFrame.u32Height - 1));

  // 推理图坐标 → 旋转后坐标（缩放到裁剪区域尺寸）
  float scaled_x = measure_x;
  float scaled_y = measure_y;
  if (state->crop_size > 0 && state->crop_scale > 1e-6f) {
    scaled_x = measure_x * state->crop_scale;
    scaled_y = measure_y * state->crop_scale;
  }

  // 旋转后坐标 → 裁剪区域坐标（反旋转 90° CW = 90° CCW）
  float S = (float)state->crop_size;
  float unrot_x = scaled_y;
  float unrot_y = S - 1.0f - scaled_x;

  // 裁剪区域坐标 → 原始传感器坐标
  float orig_x = unrot_x + (float)state->crop_x;
  float orig_y = unrot_y + (float)state->crop_y;

  float angle_x = 0.0f;
  float angle_y = 0.0f;
  if (state->camera.fx > 0.0f && state->camera.fy > 0.0f) {
    angle_x = atan((orig_x - state->camera.cx) / state->camera.fx);
    angle_y = atan((orig_y - state->camera.cy) / state->camera.fy);
  }

  *distance_m = 0.0f;
  *distance_valid = false;
  float bbox_w = obj->bbox.x2 - obj->bbox.x1;
  if (bbox_w > 0.0f && bbox_h > 0.0f) {
    float scale = 1.0f;
    if (state->crop_size > 0 && state->crop_scale > 1e-6f) {
      scale = state->crop_scale;
    }
    float diameter_px = 0.5f * (bbox_w + bbox_h) * scale;
    float fx = state->camera.fx;
    float fy = state->camera.fy;
    float f = 0.5f * (fx + fy);
    if (diameter_px >= TARGET_MIN_DIAMETER_PX && f > 1e-6f && fx > 1e-6f && fy > 1e-6f) {
      float z = (f * TARGET_DIAMETER_M) / diameter_px;
      float x = ((orig_x - state->camera.cx) / fx) * z;
      float y = ((orig_y - state->camera.cy) / fy) * z;
      *distance_m = sqrtf(x * x + y * y + z * z);
      *distance_valid = true;
    }
  }

  // 相机旋转 90°：sensor x→pitch(右正), sensor y→yaw(上正, 取负)
  *yaw_deg = -angle_y * 180.0f / (float)M_PI - state->optical_axis_yaw_comp_deg;
  *pitch_deg = angle_x * 180.0f / (float)M_PI - state->optical_axis_pitch_comp_deg;
  return true;
}

// 初始化 YOLOv8 预处理/后处理参数
// - 统一缩放与均值
// - 设置类别数与最大检测数
// - 配置阈值/NMS
CVI_S32 init_tdl_param(cvitdl_handle_t tdl_handle) {
  InputPreParam preprocess_cfg =
      CVI_TDL_GetPreParam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION);

  for (int i = 0; i < 3; i++) {
    preprocess_cfg.factor[i] = MODEL_SCALE;
    preprocess_cfg.mean[i] = MODEL_MEAN;
  }
  preprocess_cfg.format = PIXEL_FORMAT_RGB_888_PLANAR;

  printf("setup yolov8 param \n");
  CVI_S32 ret =
      CVI_TDL_SetPreParam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, preprocess_cfg);
  if (ret != CVI_SUCCESS) {
    printf("Can not set yolov8 preprocess parameters %#x\n", ret);
    return ret;
  }

  cvtdl_det_algo_param_t yolov8_param =
      CVI_TDL_GetDetectionAlgoParam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION);
  yolov8_param.cls = MODEL_CLASS_CNT;
  yolov8_param.max_det = MODEL_MAX_DET;

  printf("setup yolov8 algorithm param \n");
  ret = CVI_TDL_SetDetectionAlgoParam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION,
                                      yolov8_param);
  if (ret != CVI_SUCCESS) {
    printf("Can not set yolov8 algorithm parameters %#x\n", ret);
    return ret;
  }

  printf("yolov8 algorithm parameters setup success!\n");
  return ret;
}

// 推理线程：从 VPSS 取帧 -> 推理 -> 发送 UART -> 缓存检测结果
void *run_tdl(void *args) {
  printf("Enter TDL thread\n");
  TdlThreadArgs *thread_args = (TdlThreadArgs *)args;

  cvitdl_handle_t tdl_handle = thread_args->tdl_handle;
  State *state = thread_args->state;

  VIDEO_FRAME_INFO_S stFrame;
  cvtdl_object_t stObjMeta = {0};

  CVI_S32 s32Ret;
  static bool has_last_ts = false;
  static struct timespec last_ts;
  static float last_distance_m = 0.0f;
  static bool last_distance_valid = false;
  int confirm_count = 0;
  int lost_count = 0;
  bool confirmed = false;

  while (!state_should_exit(state)) {
    // static struct timespec time_stamp, last_time_stamp;
    // static double average_fps = 0.0;
    // clock_gettime(CLOCK_MONOTONIC, &time_stamp);
    // double time_used = (time_stamp.tv_sec - last_time_stamp.tv_sec) +
    //                    (time_stamp.tv_nsec - last_time_stamp.tv_nsec) / 1000000000.0;

    // average_fps = (average_fps * 9 + 1.0 / time_used) / 10;
    // printf("Time interval: %f seconds, FPS: %f, Average FPS: %f\n", time_used, 1.0 / time_used,
    //        average_fps);
    // last_time_stamp = time_stamp;

    // 从 VPSS Chn1 获取推理输入帧
    s32Ret = CVI_VPSS_GetChnFrame(0, VPSS_CHN1, &stFrame, 2000);
    if (s32Ret != CVI_SUCCESS) {
      printf("CVI_VPSS_GetChnFrame failed with %#x\n", s32Ret);
      goto get_frame_failed;
    }

    memset(&stObjMeta, 0, sizeof(cvtdl_object_t));
    // 执行推理
    s32Ret = CVI_TDL_Detection(tdl_handle, &stFrame, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION,
                               &stObjMeta);
    if (s32Ret != CVI_TDL_SUCCESS) {
      printf("inference failed!, ret=%x\n", s32Ret);
      goto inf_error;
    }

    map_frame_virtual_addr(&stFrame);

    // 计算时间间隔（用于卡尔曼预测）
    float dt = calc_delta_time(&state->kf_cfg, &last_ts, &has_last_ts);

    // 卡尔曼滤波 + 简单状态机（确认/丢失）
    bool has_detection = (stObjMeta.size > 0);
    float meas_yaw = 0.0f;
    float meas_pitch = 0.0f;
    float meas_distance_m = 0.0f;
    bool meas_distance_valid = false;
    if (has_detection) {
      has_detection = measure_best_angle(&stFrame, &stObjMeta, state, dt,
                     state->kf_cfg.aspect_ratio_thresh, &meas_yaw, &meas_pitch,
                     &meas_distance_m, &meas_distance_valid);
      if (has_detection && meas_distance_valid) {
        last_distance_m = meas_distance_m;
        last_distance_valid = true;
      }
    }

    unmap_frame_virtual_addr(&stFrame);

    if (has_detection) {
      bool just_initialized = false;
      if (!state->kf.initialized) {
        kalman_init(&state->kf, &state->kf_cfg, meas_yaw, meas_pitch);
        confirm_count = 1;
        lost_count = 0;
        confirmed = (state->kf_cfg.min_confirm_frames <= 1);
        just_initialized = true;
      } else {
        kalman_predict(&state->kf, &state->kf_cfg, dt);
        kalman_update(&state->kf, &state->kf_cfg, meas_yaw, meas_pitch);
        lost_count = 0;
      }

      if (!confirmed && !just_initialized) {
        confirm_count++;
        if (confirm_count >= state->kf_cfg.min_confirm_frames) {
          confirmed = true;
        }
      }
    } else if (state->kf.initialized) {
      if (!confirmed) {
        state->kf.initialized = false;
        confirm_count = 0;
        lost_count = 0;
      } else {
        kalman_predict(&state->kf, &state->kf_cfg, dt);
        lost_count++;
      }
    } else {
      confirm_count = 0;
      lost_count = 0;
      confirmed = false;
    }

    if (confirmed && lost_count > state->kf_cfg.lost_frames) {
      state->kf.initialized = false;
      confirm_count = 0;
      lost_count = 0;
      confirmed = false;
    }

    // 将滤波结果整理为输出（供绘制/UART）
    TrackOutput track_output = {0};
    track_output.confirmed = confirmed;
    track_output.confirm_count = confirm_count;
    track_output.lost_count = lost_count;

    if (state->kf.initialized) {
      track_output.yaw = state->kf.x[0];
      track_output.v_yaw = state->kf.x[1];
      track_output.pitch = state->kf.x[2];
      track_output.v_pitch = state->kf.x[3];

      float raw_yaw = compensated_to_raw_yaw(state, track_output.yaw);
      float raw_pitch = compensated_to_raw_pitch(state, track_output.pitch);

      angle_to_pixel(&state->camera, raw_yaw, raw_pitch, &track_output.px,
                     &track_output.py);
      angle_velocity_to_pixel_velocity(&state->camera, raw_yaw, raw_pitch,
                                       track_output.v_yaw, track_output.v_pitch, &track_output.v_px,
                                       &track_output.v_py);

      if (confirmed) {
        if (has_detection || lost_count <= state->kf_cfg.lost_frames) {
          track_output.has_output = true;
        }
      }

      // 丢帧容忍窗口内也视为“可追踪到目标”，UART 持续发送预测后的姿态。
      track_output.detected = track_output.has_output;

      if (has_detection && meas_distance_valid) {
        track_output.distance = meas_distance_m;
        track_output.distance_valid = true;
      } else if (track_output.has_output && last_distance_valid) {
        track_output.distance = last_distance_m;
        track_output.distance_valid = true;
      }
    }

    state_update_track(state, &track_output);
    struct timespec kf_ts;
    clock_gettime(CLOCK_MONOTONIC, &kf_ts);
    state_update_track_ts(state, &kf_ts);

    // 保留详细信息，便于定位
    if (stObjMeta.size != 0) {
      printf("Obj count: %d\n", stObjMeta.size);
      for (int i = 0; i < stObjMeta.size; i++) {
        int center_x = (int)(stObjMeta.info[i].bbox.x1 +
                             (stObjMeta.info[i].bbox.x2 - stObjMeta.info[i].bbox.x1) / 2);
        int center_y = (int)(stObjMeta.info[i].bbox.y1 +
                             (stObjMeta.info[i].bbox.y2 - stObjMeta.info[i].bbox.y1) / 2);
        float angle_x = 0.0f;
        float angle_y = 0.0f;
        if (state->camera.fx > 0.0f && state->camera.fy > 0.0f) {
          angle_x = atan((center_x - state->camera.cx) / state->camera.fx);
          angle_y = atan((center_y - state->camera.cy) / state->camera.fy);
        }
        float angle_x_deg = angle_x * 180.0f / (float)M_PI - state->optical_axis_yaw_comp_deg;
        float angle_y_deg = angle_y * 180.0f / (float)M_PI - state->optical_axis_pitch_comp_deg;

        printf("  Obj[%d]: Score: %f, BBox: [%f, %f, %f, %f], Angle(deg): [%.2f, %.2f]\n", i,
               stObjMeta.info[i].bbox.score, stObjMeta.info[i].bbox.x1, stObjMeta.info[i].bbox.y1,
               stObjMeta.info[i].bbox.x2, stObjMeta.info[i].bbox.y2, angle_x_deg, angle_y_deg);
      }
    }

    // 更新共享检测结果（供编码/绘制线程使用）
    state_update_objects(state, &stObjMeta);

  inf_error:
    CVI_VPSS_ReleaseChnFrame(0, 1, &stFrame);
  get_frame_failed:
    CVI_TDL_Free(&stObjMeta);
    if (s32Ret != CVI_SUCCESS) {
      state_request_exit(state);
    }
  }

  printf("Exit TDL thread\n");
  pthread_exit(NULL);
}
