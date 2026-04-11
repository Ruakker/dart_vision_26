#ifndef DART_VISION_KALMAN_H_
#define DART_VISION_KALMAN_H_

#include <stdbool.h>

// 卡尔曼滤波器配置（常速度模型）
typedef struct {
  float q_yaw;
  float q_v_yaw;
  float q_pitch;
  float q_v_pitch;
  float r_yaw;
  float r_pitch;
  float p0_yaw;
  float p0_v_yaw;
  float p0_pitch;
  float p0_v_pitch;
  int min_confirm_frames;
  int lost_frames;
  float aspect_ratio_thresh;
  float dt_default;
  float dt_min;
  float dt_max;
  float confidence_weight;
  float velocity_scale;
  float arrow_length_px;
  float arrow_head_px;
  float text_offset_px;
} KalmanConfig;

typedef struct {
  float x[4];
  float P[4][4];
  bool initialized;
} Kalman;

typedef struct {
  bool detected;
  bool has_output;
  bool distance_valid;
  bool confirmed;
  int confirm_count;
  int lost_count;
  float yaw;
  float pitch;
  float v_yaw;
  float v_pitch;
  float distance;
  float px;
  float py;
  float v_px;
  float v_py;
} TrackOutput;

void kalman_config_init(KalmanConfig *cfg);
bool load_kalman_config(const char *path, KalmanConfig *cfg);

void kalman_init(Kalman *kf, const KalmanConfig *cfg, float yaw, float pitch);
void kalman_predict(Kalman *kf, const KalmanConfig *cfg, float dt);
void kalman_update(Kalman *kf, const KalmanConfig *cfg, float yaw, float pitch);

#endif
