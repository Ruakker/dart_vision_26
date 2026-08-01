#ifndef DART_VISION_STATE_H_
#define DART_VISION_STATE_H_

#include <stdbool.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>

#include "cvi_tdl.h"
#include "dart_vision/config.h"
#include "dart_vision/kalman.h"

// 运行时共享状态（线程间通信）
typedef struct {
  volatile bool exit_requested;      // 线程退出标记
  bool raw;                          // raw模式（不叠加绘制）
  int uart_fd;                       // UART 句柄
  CameraParams camera;               // 相机内参
  float optical_axis_yaw_comp_deg;   // 光轴yaw补偿（度）
  float optical_axis_pitch_comp_deg; // 光轴pitch补偿（度）
  // 推理裁剪信息（用于把推理坐标映射回原始相机坐标系）
  CVI_S32 crop_x;
  CVI_S32 crop_y;
  CVI_U32 crop_size;
  float crop_scale;
  cvtdl_object_t last_obj;           // 最新检测结果（深拷贝）
  pthread_mutex_t obj_mutex;         // 保护 last_obj
  KalmanConfig kf_cfg;               // 卡尔曼配置
  Kalman kf;                         // 卡尔曼状态
  TrackOutput track;                 // 追踪输出（供绘制/UART）
  pthread_mutex_t track_mutex;       // 保护 track
  pthread_cond_t track_cv;           // 追踪更新通知（唤醒发送线程）
  uint64_t track_seq;                // 追踪更新序号（防止丢唤醒）
  struct timespec track_ts;          // 上次卡尔曼更新时刻（单调时钟）
  bool track_ts_valid;               // 时间戳有效标记
  float feedback_data[4];            // 最新串口反馈数据
  bool feedback_valid;               // 是否收到过串口反馈
  uint64_t feedback_seq;             // 串口反馈更新序号
  pthread_mutex_t feedback_mutex;    // 保护串口反馈数据
  bool record_enable;                // 是否录制
  FILE *record_fp;                   // 录制文件句柄
  FILE *log_fp;                      // 日志文件句柄
} State;

void state_init(State *state, bool raw);
void state_destroy(State *state);
void state_request_exit(State *state);
bool state_should_exit(const State *state);

// 更新/复制检测结果（内部带锁）
void state_update_objects(State *state, const cvtdl_object_t *src);
void state_copy_objects(State *state, cvtdl_object_t *dst);

// 更新/复制卡尔曼输出（内部带锁）
void state_update_track(State *state, const TrackOutput *src);
void state_copy_track(State *state, TrackOutput *dst);
void state_update_track_ts(State *state, const struct timespec *ts);
bool state_copy_track_ts(State *state, struct timespec *ts);

// 更新/复制串口反馈（内部带锁）
void state_update_feedback(State *state, const float data[4]);
bool state_copy_feedback(State *state, float data[4], uint64_t *seq);

#endif
