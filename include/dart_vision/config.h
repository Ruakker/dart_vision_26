#ifndef DART_VISION_CONFIG_H_
#define DART_VISION_CONFIG_H_

#include <stdbool.h>

// 模型与推理参数
#define MODEL_SCALE 0.0039216f
#define MODEL_MEAN 0.0f
#define MODEL_CLASS_CNT 1
#define MODEL_MAX_DET 3
#define MODEL_THRESH_DEFAULT 0.001f
#define MODEL_NMS_THRESH 0.5f
// 图像尺寸（推理与编码统一）
#define IMAGE_WIDTH 320
#define IMAGE_HEIGHT 320

// 绿色在 YUV 空间特征：U 和 V 分量都偏低 (< 128)
// 考虑到环境光干扰，建议设置在 110-120 之间
#define GREEN_UV_THRESH 125

// 单目测距参数：目标真实直径(米)与最小有效像素直径
#define TARGET_DIAMETER_M 0.055f
#define TARGET_MIN_DIAMETER_PX 3.0f

// 瞄点偏移：以检测框中心为基准，沿图像 y 轴向上偏移 (系数 * 框高)
// 例如 1.5f 表示使用“中心点上方 1.5 个框高”作为更新/发送位置
#define TARGET_UP_OFFSET_HEIGHT_FACTOR 1.5f

// 配置文件路径
#define CAMERA_INFO_PATH "/root/config/camera_info.json"
#define APP_CONFIG_PATH "/root/config/config.json"

// 串口默认配置
#define UART_DEVICE "/dev/ttyS0"
#define UART_BAUDRATE 115200

// 相机内参（像素坐标转换用）
typedef struct {
	float fx;
	float fy;
	float cx;
	float cy;
} CameraParams;

// 应用配置（含命令行与配置文件合并结果）
typedef struct {
	const char *model_path;
	const char *camera_info_path;
	int manual_exposure_time_us;
	int ae_target_luma;
	float optical_axis_yaw_comp_deg;
	float optical_axis_pitch_comp_deg;
	float model_thresh;
	bool record_enable;
	char record_dir[256];
	bool raw;
} AppConfig;

void config_set_defaults(AppConfig *cfg);
void config_sanitize(AppConfig *cfg);
bool load_app_config(const char *path, AppConfig *cfg);
bool load_camera_info(const char *path, CameraParams *camera);

#endif
