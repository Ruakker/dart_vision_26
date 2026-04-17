// 编码/推流线程：叠加绘制 + RTSP + 录制
#include "../include/dart_vision/venc.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <sample_comm.h>

#include <core/utils/vpss_helper.h>
#include <rtsp.h>

#include "../include/dart_vision/state.h"
#include "../include/dart_vision/tdl.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void optical_axis_comp_to_pixel(const State *state, float *px, float *py) {
  if (!state || !px || !py) {
    return;
  }
  if (state->camera.fx <= 0.0f || state->camera.fy <= 0.0f) {
    *px = state->camera.cx;
    *py = state->camera.cy;
    return;
  }
  float yaw_rad = state->optical_axis_yaw_comp_deg * (float)M_PI / 180.0f;
  float pitch_rad = state->optical_axis_pitch_comp_deg * (float)M_PI / 180.0f;
  *px = tanf(yaw_rad) * state->camera.fx + state->camera.cx;
  *py = tanf(pitch_rad) * state->camera.fy + state->camera.cy;
}

// 编码/绘制线程：从 VPSS 取帧 -> 画框 -> RTSP 推流
// 非 raw 模式时，始终叠加可视化信息。
static CVI_S32 send_frame_rtsp_record(VIDEO_FRAME_INFO_S *stVencFrame,
                                      SAMPLE_TDL_MW_CONTEXT *pstMWContext,
                                      FILE *record_fp) {
  CVI_S32 s32Ret = CVI_SUCCESS;
  CVI_S32 s32SetFrameMilliSec = 20000;
  VENC_STREAM_S stStream;
  VENC_CHN_ATTR_S stVencChnAttr;
  VENC_CHN_STATUS_S stStat;
  VENC_CHN VencChn = pstMWContext->u32VencChn;

  s32Ret = CVI_VENC_SendFrame(VencChn, stVencFrame, s32SetFrameMilliSec);
  if (s32Ret != CVI_SUCCESS) {
    printf("CVI_VENC_SendFrame failed! %d\n", s32Ret);
    return s32Ret;
  }

  s32Ret = CVI_VENC_GetChnAttr(VencChn, &stVencChnAttr);
  if (s32Ret != CVI_SUCCESS) {
    printf("CVI_VENC_GetChnAttr, VencChn[%d], s32Ret = %d\n", VencChn, s32Ret);
    return s32Ret;
  }

  s32Ret = CVI_VENC_QueryStatus(VencChn, &stStat);
  if (s32Ret != CVI_SUCCESS) {
    printf("CVI_VENC_QueryStatus failed with %#x!\n", s32Ret);
    return s32Ret;
  }

  if (!stStat.u32CurPacks) {
    return s32Ret;
  }

  stStream.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S) * stStat.u32CurPacks);
  if (stStream.pstPack == NULL) {
    printf("malloc memory failed!\n");
    return s32Ret;
  }

  s32Ret = CVI_VENC_GetStream(VencChn, &stStream, 10000);
  if (s32Ret != CVI_SUCCESS) {
    printf("CVI_VENC_GetStream failed with %#x!\n", s32Ret);
    goto send_failed;
  }

  VENC_PACK_S *ppack;
  CVI_RTSP_DATA data = {0};
  memset(&data, 0, sizeof(CVI_RTSP_DATA));

  data.blockCnt = stStream.u32PackCount;
  for (unsigned int i = 0; i < stStream.u32PackCount; i++) {
    ppack = &stStream.pstPack[i];
    data.dataPtr[i] = ppack->pu8Addr + ppack->u32Offset;
    data.dataLen[i] = ppack->u32Len - ppack->u32Offset;
    if (record_fp) {
      fwrite(data.dataPtr[i], 1, data.dataLen[i], record_fp);
    }
  }

  s32Ret =
      CVI_RTSP_WriteFrame(pstMWContext->pstRtspContext, pstMWContext->pstSession->video, &data);
  if (s32Ret != CVI_SUCCESS) {
    printf("CVI_RTSP_WriteFrame, s32Ret = %d\n", s32Ret);
    goto send_failed;
  }

send_failed:
  CVI_VENC_ReleaseStream(VencChn, &stStream);
  free(stStream.pstPack);
  stStream.pstPack = NULL;
  return s32Ret;
}

// 绘制检测框与跟踪信息（中心线、十字、速度箭头、文字）
static CVI_S32 draw_overlay(VencThreadArgs *thread_args, VIDEO_FRAME_INFO_S *stFrame,
                            cvtdl_object_t *stObjMeta) {
  if (!stFrame) {
    return CVI_FAILURE;
  }
  // 添加时间戳到左下角
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  struct tm tm_info;
  localtime_r(&ts.tv_sec, &tm_info);
  char time_str[20];
  snprintf(time_str, sizeof(time_str), "%02d:%02d.%03ld", tm_info.tm_min, tm_info.tm_sec, ts.tv_nsec / 1000000);
  CVI_TDL_Service_ObjectWriteText(time_str, 10, stFrame->stVFrame.u32Height - 10, stFrame, 255, 255, 255);

  if (!thread_args || !stFrame || !stObjMeta) {
    return CVI_FAILURE;
  }

  memset(stObjMeta, 0, sizeof(cvtdl_object_t));
  state_copy_objects(thread_args->state, stObjMeta);

  cvtdl_service_brush_t blue_brush = CVI_TDL_Service_GetDefaultBrush();
  blue_brush.color.r = 0.0f;
  blue_brush.color.g = 0.0f;
  blue_brush.color.b = 255.0f;
  blue_brush.size = 1;
  CVI_S32 s32Ret = CVI_TDL_Service_ObjectDrawRect(thread_args->service_handle, stObjMeta, stFrame,
                                                  false, blue_brush);
  if (s32Ret != CVI_TDL_SUCCESS) {
    return s32Ret;
  }

  // 画出卡尔曼滤波输出（角度对应像素 + 速度箭头）
  TrackOutput track = {0};
  state_copy_track(thread_args->state, &track);
  if (!track.has_output) {
    return CVI_SUCCESS;
  }

  // 将相机坐标系像素映射到当前输出帧坐标系（320x320）
  float disp_px = track.px;
  float disp_py = track.py;
  float disp_cx = 0.0f;
  float disp_cy = 0.0f;
  optical_axis_comp_to_pixel(thread_args->state, &disp_cx, &disp_cy);
  float disp_vx = track.v_px;
  float disp_vy = track.v_py;
  if (thread_args->state->crop_size > 0 && thread_args->state->crop_scale > 1e-6f) {
    float inv_scale = 1.0f / thread_args->state->crop_scale;
    disp_px = (track.px - (float)thread_args->state->crop_x) * inv_scale;
    disp_py = (track.py - (float)thread_args->state->crop_y) * inv_scale;
    disp_cx = (disp_cx - (float)thread_args->state->crop_x) * inv_scale;
    disp_cy = (disp_cy - (float)thread_args->state->crop_y) * inv_scale;
    disp_vx = track.v_px * inv_scale;
    disp_vy = track.v_py * inv_scale;
  }

  cvtdl_service_brush_t red_brush = CVI_TDL_Service_GetDefaultBrush();
  red_brush.color.r = 255.0f;
  red_brush.color.g = 0.0f;
  red_brush.color.b = 0.0f;
  red_brush.size = 1;

  cvtdl_service_brush_t yellow_brush = CVI_TDL_Service_GetDefaultBrush();
  yellow_brush.color.r = 255.0f;
  yellow_brush.color.g = 255.0f;
  yellow_brush.color.b = 0.0f;
  yellow_brush.size = 1;

  // 光心与目标中心连线
  float x_line[2] = {disp_cx, disp_px};
  float y_line[2] = {disp_cy, disp_py};
  cvtdl_pts_t line_pts = {0};
  line_pts.x = x_line;
  line_pts.y = y_line;
  line_pts.size = 2;
  CVI_TDL_Service_DrawPolygon(thread_args->service_handle, stFrame, &line_pts, red_brush);
  // 目标中心十字
  float cross = 4.0f;
  float x_h[2] = {disp_px - cross, disp_px + cross};
  float y_h[2] = {disp_py, disp_py};
  cvtdl_pts_t h_pts = {0};
  h_pts.x = x_h;
  h_pts.y = y_h;
  h_pts.size = 2;
  CVI_TDL_Service_DrawPolygon(thread_args->service_handle, stFrame, &h_pts, red_brush);

  float x_v[2] = {disp_px, disp_px};
  float y_v[2] = {disp_py - cross, disp_py + cross};
  cvtdl_pts_t v_pts = {0};
  v_pts.x = x_v;
  v_pts.y = y_v;
  v_pts.size = 2;
  CVI_TDL_Service_DrawPolygon(thread_args->service_handle, stFrame, &v_pts, red_brush);

  // 光心十字
  float cx = disp_cx;
  float cy = disp_cy;
  float x_ch[2] = {cx - cross, cx + cross};
  float y_ch[2] = {cy, cy};
  cvtdl_pts_t ch_pts = {0};
  ch_pts.x = x_ch;
  ch_pts.y = y_ch;
  ch_pts.size = 2;
  CVI_TDL_Service_DrawPolygon(thread_args->service_handle, stFrame, &ch_pts, red_brush);

  float x_cv[2] = {cx, cx};
  float y_cv[2] = {cy - cross, cy + cross};
  cvtdl_pts_t cv_pts = {0};
  cv_pts.x = x_cv;
  cv_pts.y = y_cv;
  cv_pts.size = 2;
  CVI_TDL_Service_DrawPolygon(thread_args->service_handle, stFrame, &cv_pts, red_brush);

  // 速度箭头
  float speed = hypotf(disp_vx, disp_vy);
  if (speed > 1e-3f) {
    float dir_x = disp_vx / speed;
    float dir_y = disp_vy / speed;
    float arrow_len = speed * 0.5f;
    if (arrow_len < 6.0f) {
      arrow_len = 6.0f;
    } else if (arrow_len > 80.0f) {
      arrow_len = 80.0f;
    }
    float tip_x = disp_px + dir_x * arrow_len;
    float tip_y = disp_py + dir_y * arrow_len;

    // 速度箭头主干
    float x_body[2] = {disp_px, tip_x};
    float y_body[2] = {disp_py, tip_y};
    cvtdl_pts_t body_pts = {0};
    body_pts.x = x_body;
    body_pts.y = y_body;
    body_pts.size = 2;
    CVI_TDL_Service_DrawPolygon(thread_args->service_handle, stFrame, &body_pts, yellow_brush);

    float head = 4.0f;
    float perp_x = -dir_y;
    float perp_y = dir_x;
    float left_x = tip_x - dir_x * head + perp_x * (head * 0.6f);
    float left_y = tip_y - dir_y * head + perp_y * (head * 0.6f);
    float right_x = tip_x - dir_x * head - perp_x * (head * 0.6f);
    float right_y = tip_y - dir_y * head - perp_y * (head * 0.6f);

    float x_pts[3] = {tip_x, left_x, right_x};
    float y_pts[3] = {tip_y, left_y, right_y};
    cvtdl_pts_t arrow_pts = {0};
    arrow_pts.x = x_pts;
    arrow_pts.y = y_pts;
    arrow_pts.size = 3;

    CVI_TDL_Service_DrawPolygon(thread_args->service_handle, stFrame, &arrow_pts, yellow_brush);
  }

  char text[128];
  snprintf(text, sizeof(text), "%.2f %.2f", track.yaw, track.pitch);
  CVI_TDL_Service_ObjectWriteText(text, 1, 25, stFrame, 255, 0, 0);
  if (track.distance_valid) {
    snprintf(text, sizeof(text), "%.3fm", track.distance);
  } else {
    snprintf(text, sizeof(text), "N/A");
  }
  CVI_TDL_Service_ObjectWriteText(text, 1, 55, stFrame, 255, 0, 0);

  return CVI_SUCCESS;
}

void *run_venc(void *args) {
  printf("Enter encoder thread\n");
  VencThreadArgs *thread_args = (VencThreadArgs *)args;

  VIDEO_FRAME_INFO_S stFrame;
  CVI_S32 s32Ret;
  cvtdl_object_t stObjMeta = {0};

  while (!state_should_exit(thread_args->state)) {
    // 从 VPSS Chn0 获取编码/显示帧
    s32Ret = CVI_VPSS_GetChnFrame(0, 0, &stFrame, 2000);
    if (s32Ret != CVI_SUCCESS) {
      printf("CVI_VPSS_GetChnFrame chn0 failed with %#x\n", s32Ret);
      state_request_exit(thread_args->state);
      break;
    }

    if (!thread_args->state->raw) {
      s32Ret = draw_overlay(thread_args, &stFrame, &stObjMeta);
      if (s32Ret != CVI_SUCCESS) {
        CVI_VPSS_ReleaseChnFrame(0, 0, &stFrame);
        printf("Draw frame fail!, ret=%x\n", s32Ret);
        goto error;
      }
    }

    // 推流输出
    s32Ret = send_frame_rtsp_record(&stFrame, thread_args->mw_context,
                         thread_args->state->record_enable
                           ? thread_args->state->record_fp
                           : NULL);
    if (s32Ret != CVI_SUCCESS) {
      CVI_VPSS_ReleaseChnFrame(0, 0, &stFrame);
      printf("Send Output Frame NG, ret=%x\n", s32Ret);
      goto error;
    }

  error:
    CVI_TDL_Free(&stObjMeta);
    CVI_VPSS_ReleaseChnFrame(0, 0, &stFrame);
    if (s32Ret != CVI_SUCCESS) {
      state_request_exit(thread_args->state);
    }
  }

  printf("Exit encoder thread\n");
  pthread_exit(NULL);
}
