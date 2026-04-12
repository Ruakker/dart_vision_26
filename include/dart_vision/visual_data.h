#ifndef VISUAL_DATA_H_
#define VISUAL_DATA_H_

#include <stdint.h>

// 设置 发送次数（有限次就写正整数，无限次发送写-1） 和 每几个周期发送一次
#define CMD_SET_DETECTION -1, 1
#define CMD_ID_DETECTION 0x01

#pragma pack(push, 1)
typedef struct {
  uint8_t detected; // 是否识别到引导灯
  float yaw;     // 度
  float pitch;   // 度
  float v_yaw;   // 度/秒
  float v_pitch; // 度/秒
  float distance; // 米（目标到相机光心距离）
} DetectionPacket_t;
#pragma pack(pop)


#define CMD_SET_FEEDBACK -1, 1
#define CMD_ID_FEEDBACK 0x02

#pragma pack(push, 1)
typedef struct {
    float u;
    float v;
    float w;
    float Pitch;
    float Yaw;
} FeedbackPacket_t;
#pragma pack(pop)

#endif
