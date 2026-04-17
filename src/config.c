// 参数配置：相机参数与配置文件读取
#include "../include/dart_vision/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/cJSON.h"

// 读取文件到内存（返回的 buffer 需由调用者 free）
static char *read_file_all(const char *path, size_t *size) {
  if (!path) {
    return NULL;
  }

  FILE *fp = fopen(path, "rb");
  if (!fp) {
    return NULL;
  }

  fseek(fp, 0, SEEK_END);
  long fsize = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if (fsize <= 0) {
    fclose(fp);
    return NULL;
  }

  char *buffer = (char *)malloc((size_t)fsize + 1);
  if (!buffer) {
    fclose(fp);
    return NULL;
  }

  size_t read_bytes = fread(buffer, 1, (size_t)fsize, fp);
  fclose(fp);
  buffer[read_bytes] = '\0';
  if (size) {
    *size = read_bytes;
  }
  return buffer;
}

// JSON 读取工具（不存在/类型不匹配时不改动）
static void json_read_int(cJSON *obj, const char *key, int *out) {
  cJSON *item = cJSON_GetObjectItem(obj, key);
  if (cJSON_IsNumber(item) && out) {
    *out = item->valueint;
  }
}

static void json_read_float(cJSON *obj, const char *key, float *out) {
  cJSON *item = cJSON_GetObjectItem(obj, key);
  if (cJSON_IsNumber(item) && out) {
    *out = (float)item->valuedouble;
  }
}

static void json_read_bool(cJSON *obj, const char *key, bool *out) {
  cJSON *item = cJSON_GetObjectItem(obj, key);
  if (out) {
    if (cJSON_IsBool(item)) {
      *out = cJSON_IsTrue(item);
    } else if (cJSON_IsNumber(item)) {
      *out = (item->valueint != 0);
    }
  }
}

static void json_read_string(cJSON *obj, const char *key, char *out, size_t out_size) {
  cJSON *item = cJSON_GetObjectItem(obj, key);
  if (cJSON_IsString(item) && item->valuestring && out && out_size > 0) {
    snprintf(out, out_size, "%s", item->valuestring);
  }
}

void config_set_defaults(AppConfig *cfg) {
  if (!cfg) {
    return;
  }
  cfg->manual_exposure_time_us = 500;
  cfg->ae_target_luma = 30;
  cfg->optical_axis_yaw_comp_deg = 0.0f;
  cfg->optical_axis_pitch_comp_deg = 0.0f;
  cfg->model_thresh = MODEL_THRESH_DEFAULT;
  cfg->record_enable = false;
  snprintf(cfg->record_dir, sizeof(cfg->record_dir), "%s", "/root");
}

void config_sanitize(AppConfig *cfg) {
  if (!cfg) {
    return;
  }
  if (cfg->manual_exposure_time_us < 1) cfg->manual_exposure_time_us = 1;
  if (cfg->ae_target_luma < 1) cfg->ae_target_luma = 1;
  if (cfg->ae_target_luma > 255) cfg->ae_target_luma = 255;
  if (cfg->model_thresh < 0.0f) cfg->model_thresh = 0.0f;
  if (cfg->model_thresh > 1.0f) cfg->model_thresh = 1.0f;
}

bool load_app_config(const char *path, AppConfig *cfg) {
  if (!path || !cfg) {
    return false;
  }

  char *buffer = read_file_all(path, NULL);
  if (!buffer) {
    return false;
  }

  cJSON *root = cJSON_Parse(buffer);
  free(buffer);
  if (!root) {
    return false;
  }

  cJSON *exposure = cJSON_GetObjectItem(root, "exposure");
  if (cJSON_IsObject(exposure)) {
    json_read_int(exposure, "manual_exposure_time_us", &cfg->manual_exposure_time_us);
    json_read_int(exposure, "auto_target_luma", &cfg->ae_target_luma);
  }

  cJSON *model = cJSON_GetObjectItem(root, "model");
  if (cJSON_IsObject(model)) {
    cJSON *threshold = cJSON_GetObjectItem(model, "threshold");
    if (cJSON_IsNumber(threshold)) {
      cfg->model_thresh = (float)threshold->valuedouble;
    }
  }

  cJSON *optical_axis = cJSON_GetObjectItem(root, "optical_axis_compensation");
  if (cJSON_IsObject(optical_axis)) {
    json_read_float(optical_axis, "yaw_deg", &cfg->optical_axis_yaw_comp_deg);
    json_read_float(optical_axis, "pitch_deg", &cfg->optical_axis_pitch_comp_deg);
  }

  cJSON *record = cJSON_GetObjectItem(root, "record");
  if (cJSON_IsObject(record)) {
    json_read_bool(record, "enable", &cfg->record_enable);
    json_read_string(record, "path", cfg->record_dir, sizeof(cfg->record_dir));
  }

  cJSON_Delete(root);
  return true;
}

bool load_camera_info(const char *path, CameraParams *camera) {
  if (!path || !camera) {
    return false;
  }
  char *buffer = read_file_all(path, NULL);
  if (!buffer) {
    printf("Failed to open camera info file: %s\n", path);
    return false;
  }

  // 仅解析 camera_matrix 的 9 个数字（K 矩阵）
  const char *p = strstr(buffer, "\"camera_matrix\"");
  if (!p) {
    free(buffer);
    return false;
  }

  p = strchr(p, '[');
  if (!p) {
    free(buffer);
    return false;
  }

  double vals[9];
  int count = 0;
  while (*p && count < 9) {
    if ((*p >= '0' && *p <= '9') || *p == '-' || *p == '.') {
      char *end = NULL;
      vals[count++] = strtod(p, &end);
      p = end;
    } else {
      p++;
    }
  }

  free(buffer);

  if (count < 6) {
    return false;
  }

  camera->fx = (float)vals[0];
  camera->cx = (float)vals[2];
  camera->fy = (float)vals[4];
  camera->cy = (float)vals[5];

  if (camera->fx <= 0.0f || camera->fy <= 0.0f) {
    return false;
  }

  printf("Camera params loaded: fx=%.3f fy=%.3f cx=%.3f cy=%.3f\n",
         camera->fx, camera->fy, camera->cx, camera->cy);
  return true;
}
