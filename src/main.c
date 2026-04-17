// 主进程：初始化 -> 启动线程 -> 等待退出 -> 释放资源
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include <core/utils/vpss_helper.h>
#include <sample_comm.h>

#include "../include/dart_vision/config.h"
#include "../include/dart_vision/kalman.h"
#include "../include/dart_vision/state.h"
#include "../include/dart_vision/tdl.h"
#include "../include/dart_vision/uart.h"
#include "../include/dart_vision/venc.h"
#include "middleware_utils.h"
#include "sample_utils.h"
#include "cvi_ae.h"
#include "cvi_comm_isp.h"
#include "cvi_isp.h"
#include "dart_vision/visual_data.h"
#include "tool/drv_cmd_com.h"

// 全局指针用于信号处理回调中安全通知退出
static State *g_state = NULL;

// 处理 SIGINT/SIGTERM，触发线程安全退出
static void handle_signal(int signo) {
  if (SIGINT == signo || SIGTERM == signo) {
    state_request_exit(g_state);
  }
}

// 打印使用说明
static void print_usage(const char *prog) {
  printf(
  "\nUsage: %s YOLOV8_MODEL_PATH [--raw].\n\n"
      "\tYOLOV8_MODEL_PATH, path to yolov8n model.\n"
  "\t--raw, record/stream raw frames without any overlay.\n",
      prog);
}

// 解析命令行参数
// 支持：
//  1) model
//  2) model --raw
static bool parse_args(int argc, char **argv, AppConfig *cfg) {
  if (!cfg) {
    return false;
  }

  memset(cfg, 0, sizeof(*cfg));
  cfg->raw = false;

  if (argc < 2) {
    return false;
  }

  cfg->model_path = argv[1];
  for (int i = 2; i < argc; i++) {
    if (strcmp(argv[i], "--raw") == 0) {
      cfg->raw = true;
    } else {
      return false;
    }
  }

  return true;
}

// 初始化中间件：VI/VPSS/VENC/RTSP 等
// 该配置决定 VPSS 输出通道尺寸以及 VBPool 分配策略。
static CVI_S32 init_middleware(const AppConfig *cfg, SAMPLE_TDL_MW_CONTEXT *mw_context,
                               SIZE_S *venc_size, CVI_S32 *out_crop_x, CVI_S32 *out_crop_y,
                               CVI_U32 *out_crop_size) {
  SAMPLE_TDL_MW_CONFIG_S stMWConfig = {0};
  CVI_S32 s32Ret = SAMPLE_TDL_Get_VI_Config(&stMWConfig.stViConfig);
  if (s32Ret != CVI_SUCCESS || stMWConfig.stViConfig.s32WorkingViNum <= 0) {
    printf("Failed to get senor infomation from ini file (/mnt/data/sensor_cfg.ini).\n");
    return CVI_FAILURE;
  }

  PIC_SIZE_E enPicSize;
  s32Ret = SAMPLE_COMM_VI_GetSizeBySensor(stMWConfig.stViConfig.astViInfo[0].stSnsInfo.enSnsType,
                                          &enPicSize);
  if (s32Ret != CVI_SUCCESS) {
    printf("Cannot get senor size\n");
    return CVI_FAILURE;
  }

  SIZE_S stSensorSize;
  s32Ret = SAMPLE_COMM_SYS_GetPicSize(enPicSize, &stSensorSize);
  if (s32Ret != CVI_SUCCESS) {
    printf("Cannot get senor size\n");
    return CVI_FAILURE;
  }

  // 固定编码尺寸
  venc_size->u32Width = IMAGE_WIDTH;
  venc_size->u32Height = IMAGE_HEIGHT;

  stMWConfig.stVBPoolConfig.u32VBPoolCount = 3;

  // VBPool 0: VPSS Grp0 Chn0 (编码/显示)
  stMWConfig.stVBPoolConfig.astVBPoolSetup[0].enFormat = VI_PIXEL_FORMAT;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[0].u32BlkCount = 5;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[0].u32Height = venc_size->u32Height;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[0].u32Width = venc_size->u32Width;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[0].bBind = true;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[0].u32VpssChnBinding = VPSS_CHN0;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[0].u32VpssGrpBinding = (VPSS_GRP)0;

  // VBPool 1: VPSS Grp0 Chn1 (推理输入)
  stMWConfig.stVBPoolConfig.astVBPoolSetup[1].enFormat = VI_PIXEL_FORMAT;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[1].u32BlkCount = 5;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[1].u32Height = venc_size->u32Height;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[1].u32Width = venc_size->u32Width;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[1].bBind = true;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[1].u32VpssChnBinding = VPSS_CHN1;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[1].u32VpssGrpBinding = (VPSS_GRP)0;

  // VBPool 2: TDL 预处理
  stMWConfig.stVBPoolConfig.astVBPoolSetup[2].enFormat = PIXEL_FORMAT_BGR_888_PLANAR;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[2].u32BlkCount = 3;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[2].u32Height = IMAGE_HEIGHT;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[2].u32Width = IMAGE_WIDTH;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[2].bBind = false;

  // VPSS Grp0 配置
  stMWConfig.stVPSSPoolConfig.u32VpssGrpCount = 1;
#ifndef CV186X
  stMWConfig.stVPSSPoolConfig.stVpssMode.aenInput[0] = VPSS_INPUT_MEM;
  stMWConfig.stVPSSPoolConfig.stVpssMode.enMode = VPSS_MODE_DUAL;
  stMWConfig.stVPSSPoolConfig.stVpssMode.ViPipe[0] = 0;
  stMWConfig.stVPSSPoolConfig.stVpssMode.aenInput[1] = VPSS_INPUT_ISP;
  stMWConfig.stVPSSPoolConfig.stVpssMode.ViPipe[1] = 0;
#endif

  SAMPLE_TDL_VPSS_CONFIG_S *pstVpssConfig = &stMWConfig.stVPSSPoolConfig.astVpssConfig[0];
  pstVpssConfig->bBindVI = true;
  VPSS_GRP_DEFAULT_HELPER2(&pstVpssConfig->stVpssGrpAttr, stSensorSize.u32Width,
                           stSensorSize.u32Height, VI_PIXEL_FORMAT, 1);
  pstVpssConfig->u32ChnCount = 2;
  pstVpssConfig->u32ChnBindVI = 0;
  VPSS_CHN_DEFAULT_HELPER(&pstVpssConfig->astVpssChnAttr[0], venc_size->u32Width,
                          venc_size->u32Height, VI_PIXEL_FORMAT, true);
  VPSS_CHN_DEFAULT_HELPER(&pstVpssConfig->astVpssChnAttr[1], venc_size->u32Width,
                          venc_size->u32Height, VI_PIXEL_FORMAT, true);

  // VENC/RTSP 默认配置
  SAMPLE_TDL_Get_Input_Config(&stMWConfig.stVencConfig.stChnInputCfg);
  stMWConfig.stVencConfig.stChnInputCfg.width = venc_size->u32Width;
  stMWConfig.stVencConfig.stChnInputCfg.height = venc_size->u32Height;
  stMWConfig.stVencConfig.u32FrameWidth = venc_size->u32Width;
  stMWConfig.stVencConfig.u32FrameHeight = venc_size->u32Height;
  SAMPLE_TDL_Get_RTSP_Config(&stMWConfig.stRTSPConfig.stRTSPConfig);

  s32Ret = SAMPLE_TDL_Init_WM(&stMWConfig, mw_context);
  if (s32Ret != CVI_SUCCESS) {
    printf("init middleware failed! ret=%x\n", s32Ret);
    return CVI_FAILURE;
  }

  // VPSS Chn1 中心裁剪 480x480，再缩放到推理输入尺寸 (320x320)
  CVI_U32 crop_size = CROP_SIZE;
  if (crop_size > stSensorSize.u32Width || crop_size > stSensorSize.u32Height) {
    crop_size = (stSensorSize.u32Width < stSensorSize.u32Height) ? stSensorSize.u32Width
                                                                 : stSensorSize.u32Height;
  }
  // 对齐到偶数，避免硬件对齐问题
  crop_size &= ~1U;
  CVI_S32 crop_x = (CVI_S32)((stSensorSize.u32Width - crop_size) / 2);
  CVI_S32 crop_y = (CVI_S32)((stSensorSize.u32Height - crop_size) / 2);
  crop_x &= ~1;
  crop_y &= ~1;

  if (out_crop_x) {
    *out_crop_x = crop_x;
  }
  if (out_crop_y) {
    *out_crop_y = crop_y;
  }
  if (out_crop_size) {
    *out_crop_size = crop_size;
  }

  VPSS_CROP_INFO_S stChnCrop = {0};
  stChnCrop.bEnable = CVI_TRUE;
  stChnCrop.enCropCoordinate = VPSS_CROP_ABS_COOR;
  stChnCrop.stCropRect.s32X = crop_x;
  stChnCrop.stCropRect.s32Y = crop_y;
  stChnCrop.stCropRect.u32Width = crop_size;
  stChnCrop.stCropRect.u32Height = crop_size;

  // 对显示/编码通道同样裁剪为方形，避免 RTSP 画面出现非对齐填充
  s32Ret = CVI_VPSS_SetChnCrop(0, VPSS_CHN0, &stChnCrop);
  if (s32Ret != CVI_SUCCESS) {
    printf("CVI_VPSS_SetChnCrop chn0 failed with %#x\n", s32Ret);
    return CVI_FAILURE;
  }

  s32Ret = CVI_VPSS_SetChnCrop(0, VPSS_CHN1, &stChnCrop);
  if (s32Ret != CVI_SUCCESS) {
    printf("CVI_VPSS_SetChnCrop chn1 failed with %#x\n", s32Ret);
    return CVI_FAILURE;
  }

  return CVI_SUCCESS;
}

// 初始化 TDL：创建句柄、设置 VBPool、加载模型与参数
static CVI_S32 init_tdl(const AppConfig *cfg, cvitdl_handle_t *tdl_handle,
                        cvitdl_service_handle_t *service_handle) {
  CVI_S32 s32Ret;

  *tdl_handle = NULL;
  *service_handle = NULL;

  // Create TDL handle and assign VPSS Grp1 Device 0 to TDL SDK
  GOTO_IF_FAILED(CVI_TDL_CreateHandle2(tdl_handle, 1, 0), s32Ret, create_tdl_fail);
  GOTO_IF_FAILED(CVI_TDL_SetVBPool(*tdl_handle, 0, 2), s32Ret, create_service_fail);

  CVI_TDL_SetVpssTimeout(*tdl_handle, 1000);

  GOTO_IF_FAILED(CVI_TDL_Service_CreateHandle(service_handle, *tdl_handle), s32Ret,
                 create_service_fail);

  GOTO_IF_FAILED(init_tdl_param(*tdl_handle), s32Ret, setup_tdl_fail);

  GOTO_IF_FAILED(
      CVI_TDL_OpenModel(*tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, cfg->model_path),
      s32Ret, setup_tdl_fail);

  CVI_TDL_SetModelThreshold(*tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION,
                            cfg->model_thresh);
  CVI_TDL_SetModelNmsThreshold(*tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION,
                               MODEL_NMS_THRESH);

  return CVI_SUCCESS;

setup_tdl_fail:
  CVI_TDL_Service_DestroyHandle(*service_handle);
create_service_fail:
  CVI_TDL_DestroyHandle(*tdl_handle);
create_tdl_fail:
  return s32Ret;
}

// -------------------- 状态管理 --------------------
// 初始化状态结构体与互斥锁
void state_init(State *state, bool raw) {
  memset(state, 0, sizeof(*state));
  state->raw = raw;
  state->uart_fd = -1;
  state->record_enable = false;
  state->record_fp = NULL;
  state->log_fp = NULL;
  state->optical_axis_yaw_comp_deg = 0.0f;
  state->optical_axis_pitch_comp_deg = 0.0f;
  state->crop_x = 0;
  state->crop_y = 0;
  state->crop_size = 0;
  state->crop_scale = 1.0f;
  pthread_mutex_init(&state->obj_mutex, NULL);
  pthread_mutex_init(&state->track_mutex, NULL);
  pthread_cond_init(&state->track_cv, NULL);
  kalman_config_init(&state->kf_cfg);
  state->kf.initialized = false;
  memset(&state->track, 0, sizeof(state->track));
  state->track_ts_valid = false;
  state->track_seq = 0;
}

// 释放状态结构体内部资源
void state_destroy(State *state) {
  pthread_mutex_lock(&state->obj_mutex);
  CVI_TDL_Free(&state->last_obj);
  pthread_mutex_unlock(&state->obj_mutex);
  pthread_mutex_destroy(&state->obj_mutex);
  pthread_cond_destroy(&state->track_cv);
  pthread_mutex_destroy(&state->track_mutex);
  if (state->record_fp) {
    fclose(state->record_fp);
    state->record_fp = NULL;
  }
  if (state->log_fp) {
    fclose(state->log_fp);
    state->log_fp = NULL;
  }
  close(state->uart_fd);
  state->uart_fd = -1;
}

// 线程安全标记退出
void state_request_exit(State *state) {
  if (state) {
    state->exit_requested = true;
    pthread_mutex_lock(&state->track_mutex);
    pthread_cond_broadcast(&state->track_cv);
    pthread_mutex_unlock(&state->track_mutex);
  }
}

// 读取退出标记
bool state_should_exit(const State *state) { return state ? state->exit_requested : true; }

// 更新检测结果（深拷贝）
void state_update_objects(State *state, const cvtdl_object_t *src) {
  if (!state || !src) {
    return;
  }

  pthread_mutex_lock(&state->obj_mutex);
  CVI_TDL_Free(&state->last_obj);
  memset(&state->last_obj, 0, sizeof(state->last_obj));
  if (src->info) {
    CVI_TDL_CopyObjectMeta(src, &state->last_obj);
  }
  pthread_mutex_unlock(&state->obj_mutex);
}

// 复制检测结果到外部对象
void state_copy_objects(State *state, cvtdl_object_t *dst) {
  if (!state || !dst) {
    return;
  }

  pthread_mutex_lock(&state->obj_mutex);
  if (state->last_obj.info) {
    CVI_TDL_CopyObjectMeta(&state->last_obj, dst);
  }
  pthread_mutex_unlock(&state->obj_mutex);
}

// 更新滤波输出
void state_update_track(State *state, const TrackOutput *src) {
  if (!state || !src) {
    return;
  }

  pthread_mutex_lock(&state->track_mutex);
  state->track = *src;
  pthread_mutex_unlock(&state->track_mutex);
}

// 复制滤波输出
void state_copy_track(State *state, TrackOutput *dst) {
  if (!state || !dst) {
    return;
  }

  pthread_mutex_lock(&state->track_mutex);
  *dst = state->track;
  pthread_mutex_unlock(&state->track_mutex);
}

// 更新卡尔曼更新时间戳
void state_update_track_ts(State *state, const struct timespec *ts) {
  if (!state || !ts) {
    return;
  }

  pthread_mutex_lock(&state->track_mutex);
  state->track_ts = *ts;
  state->track_ts_valid = true;
  state->track_seq++;
  pthread_cond_signal(&state->track_cv);
  pthread_mutex_unlock(&state->track_mutex);
}

// 复制卡尔曼更新时间戳
bool state_copy_track_ts(State *state, struct timespec *ts) {
  if (!state || !ts) {
    return false;
  }

  bool valid = false;
  pthread_mutex_lock(&state->track_mutex);
  if (state->track_ts_valid) {
    *ts = state->track_ts;
    valid = true;
  }
  pthread_mutex_unlock(&state->track_mutex);
  return valid;
}

// -------------------- 录制 --------------------
// 在目录下按序号创建录制文件（record_0000.h264 ...）
static FILE *open_record_file(const char *dir_path, FILE **log_fp) {
  if (!dir_path || dir_path[0] == '\0') {
    *log_fp = NULL;
    return NULL;
  }

  char full_path[512];
  char log_path[512];
  for (int idx = 0; idx < 10000; idx++) {
    snprintf(full_path, sizeof(full_path), "%s/record_%04d.h264", dir_path, idx);
    snprintf(log_path, sizeof(log_path), "%s/log_%04d.txt", dir_path, idx);
    struct stat st;
    if (stat(full_path, &st) == 0) {
      continue;
    }
    FILE *fp = fopen(full_path, "wb");
    if (fp) {
      *log_fp = fopen(log_path, "w");
    }
    return fp;
  }

  *log_fp = NULL;
  return NULL;
}

// -------------------- 曝光控制 --------------------
/**
 * 改进后的手动曝光设置：使用“受限自动模式”代替“纯手动模式”
 */
CVI_S32 set_manual_exposure(VI_PIPE ViPipe, CVI_U32 exposure_time_us) {
  ISP_EXPOSURE_ATTR_S stExpAttr;
  CVI_S32 s32Ret;

  // 获取当前属性
  s32Ret = CVI_ISP_GetExposureAttr(ViPipe, &stExpAttr);
  if (s32Ret != CVI_SUCCESS) {
    printf("CVI_ISP_GetExposureAttr failed with %#x\n", s32Ret);
    return s32Ret;
  }

  // 1. 设置为自动模式，利用 ISP 的内部校正逻辑消除条纹
  stExpAttr.enOpType = OP_TYPE_AUTO;
  stExpAttr.bByPass = CVI_FALSE;

  // 2. 锁死曝光时间范围 [target, target]
  stExpAttr.stAuto.stExpTimeRange.u32Max = exposure_time_us;
  stExpAttr.stAuto.stExpTimeRange.u32Min = exposure_time_us;

  // 3. 锁死增益范围 [1x, 1x]
  stExpAttr.stAuto.stSysGainRange.u32Max = 1024;
  stExpAttr.stAuto.stSysGainRange.u32Min = 1024;

  // 应用设置
  s32Ret = CVI_ISP_SetExposureAttr(ViPipe, &stExpAttr);
  if (s32Ret != CVI_SUCCESS) {
    printf("CVI_ISP_SetExposureAttr failed with %#x\n", s32Ret);
    return s32Ret;
  }

  printf("Exposure locked (Auto-Constrained): Exp=%u us, Gain=1x (No Stripes)\n", exposure_time_us);
  return CVI_SUCCESS;
}

/**
 * 自动曝光：手动设置目标亮度
 */
static CVI_S32 set_auto_exposure_target(VI_PIPE ViPipe, CVI_U8 target_luma) {
  ISP_EXPOSURE_ATTR_S stExpAttr;
  CVI_ISP_GetExposureAttr(ViPipe, &stExpAttr);

  stExpAttr.bByPass = CVI_FALSE;
  stExpAttr.enOpType = OP_TYPE_AUTO;
  stExpAttr.stAuto.u8Compensation = target_luma;

  CVI_ISP_SetExposureAttr(ViPipe, &stExpAttr);
  printf("Exposure set to Auto, target_luma=%u\n", stExpAttr.stAuto.u8Compensation);
  return CVI_SUCCESS;
}

/**
 * 关闭 TNR
 */
static CVI_S32 disable_tnr(VI_PIPE ViPipe) {
  ISP_TNR_ATTR_S stTnrAttr;
  CVI_S32 s32Ret = CVI_ISP_GetTNRAttr(ViPipe, &stTnrAttr);
  if (s32Ret != CVI_SUCCESS) {
    printf("CVI_ISP_GetTNRAttr failed: %#x\n", s32Ret);
    return s32Ret;
  }

  stTnrAttr.Enable = CVI_FALSE;
  s32Ret = CVI_ISP_SetTNRAttr(ViPipe, &stTnrAttr);
  if (s32Ret != CVI_SUCCESS) {
    printf("CVI_ISP_SetTNRAttr failed: %#x\n", s32Ret);
    return s32Ret;
  }

  printf("TNR disabled\n");
  return CVI_SUCCESS;
}

int main(int argc, char **argv) {
  AppConfig cfg;
  if (!parse_args(argc, argv, &cfg)) {
    print_usage(argv[0]);
    return CVI_TDL_FAILURE;
  }

  // 固定使用指定的相机内参路径
  cfg.camera_info_path = CAMERA_INFO_PATH;

  signal(SIGINT, handle_signal);
  signal(SIGTERM, handle_signal);

  State state;
  state_init(&state, cfg.raw);
  g_state = &state;

  if (!load_camera_info(cfg.camera_info_path, &state.camera)) {
    printf("Error: Invalid camera info file.\n");
    state_destroy(&state);
    return -1;
  }

  // 默认配置 + 配置文件覆盖
  config_set_defaults(&cfg);
  if (!load_app_config(APP_CONFIG_PATH, &cfg)) {
    printf("Warning: Failed to load app config (%s), using defaults.\n", APP_CONFIG_PATH);
  }
  config_sanitize(&cfg);
  state.optical_axis_yaw_comp_deg = cfg.optical_axis_yaw_comp_deg;
  state.optical_axis_pitch_comp_deg = cfg.optical_axis_pitch_comp_deg;
  printf("Optical axis compensation loaded: yaw=%.3f deg, pitch=%.3f deg\n",
         state.optical_axis_yaw_comp_deg, state.optical_axis_pitch_comp_deg);

  // 卡尔曼配置
  if (!load_kalman_config(APP_CONFIG_PATH, &state.kf_cfg)) {
    printf("Warning: Failed to load Kalman config (%s), using defaults.\n", APP_CONFIG_PATH);
  } else {
    printf("Kalman config loaded: %s\n", APP_CONFIG_PATH);
  }

  // 录制初始化：按开关创建文件
  state.record_enable = cfg.record_enable;
  if (cfg.record_enable && cfg.record_dir[0] != '\0') {
    state.record_fp = open_record_file(cfg.record_dir, &state.log_fp);
    if (!state.record_fp) {
      printf("Warning: Failed to open record file in dir: %s\n", cfg.record_dir);
      state.record_enable = false;
    } else {
      printf("Record enabled, dir: %s\n", cfg.record_dir);
    }
  }

  // UART 初始化
  state.uart_fd = uart_init(UART_DEVICE, UART_BAUDRATE);
  if (state.uart_fd < 0) {
    printf("Error: Failed to open UART!\n");
    return -1;
  }
  g_state = &state;
  uart_g_state = &state;
  if (!Attach_SendCmd((void*)(intptr_t)state.uart_fd, CMD_ID_DETECTION, TRANS_FOREVER, 1,
                      sizeof(DetectionPacket_t), &g_detection_payload)) {
    printf("Error: Failed to attach UART detection cmd!\n");
    close(state.uart_fd);
    return -1;
  }
  if (!Attach_Receive_callback(CMD_ID_FEEDBACK, sizeof(FeedbackPacket_t), &g_feedback_payload,
                               feedback_callback)) {
    printf("Error: Failed to attach UART feedback cmd!\n");
    close(state.uart_fd);
    return -1;
  }

  // middleware 初始化
  SAMPLE_TDL_MW_CONTEXT mw_context = {0};
  SIZE_S venc_size = {0};
  CVI_S32 crop_x = 0;
  CVI_S32 crop_y = 0;
  CVI_U32 crop_size = 0;
  if (init_middleware(&cfg, &mw_context, &venc_size, &crop_x, &crop_y, &crop_size) != CVI_SUCCESS) {
    state_destroy(&state);
    return -1;
  }

  state.crop_x = crop_x;
  state.crop_y = crop_y;
  state.crop_size = crop_size;
  state.crop_scale =
      (crop_size > 0 && IMAGE_WIDTH > 0) ? ((float)crop_size / (float)IMAGE_WIDTH) : 1.0f;

  // 关闭 TNR
  // disable_tnr(0);

  set_manual_exposure(0, (CVI_U32)cfg.manual_exposure_time_us);
  // set_auto_exposure_target(0, (CVI_U32)cfg.ae_target_luma);

  cvitdl_handle_t tdl_handle = NULL;
  cvitdl_service_handle_t service_handle = NULL;
  if (init_tdl(&cfg, &tdl_handle, &service_handle) != CVI_SUCCESS) {
    SAMPLE_TDL_Destroy_MW(&mw_context);
    state_destroy(&state);
    return -1;
  }

  pthread_t uart_recv_thread;
  pthread_t uart_send_thread;
  pthread_t venc_thread;
  pthread_t tdl_thread;

  UartThreadArgs uart_args = {
      .fd = state.uart_fd,
      .state = &state,
  };
  VencThreadArgs venc_args = {
      .mw_context = &mw_context,
      .service_handle = service_handle,
      .state = &state,
  };
  TdlThreadArgs tdl_args = {
      .tdl_handle = tdl_handle,
      .state = &state,
  };

  pthread_create(&uart_recv_thread, NULL, run_uart_recv, &uart_args);
  pthread_create(&uart_send_thread, NULL, run_uart_send, &uart_args);
  pthread_create(&venc_thread, NULL, run_venc, &venc_args);
  pthread_create(&tdl_thread, NULL, run_tdl, &tdl_args);

  pthread_join(uart_recv_thread, NULL);
  pthread_join(uart_send_thread, NULL);
  pthread_join(venc_thread, NULL);
  pthread_join(tdl_thread, NULL);

  CVI_TDL_Service_DestroyHandle(service_handle);
  CVI_TDL_DestroyHandle(tdl_handle);
  SAMPLE_TDL_Destroy_MW(&mw_context);
  state_destroy(&state);

  return 0;
}
