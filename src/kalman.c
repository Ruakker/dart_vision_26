// 简化的常速度模型卡尔曼滤波（yaw/pitch + v_yaw/v_pitch）
#include "../include/dart_vision/kalman.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/cJSON.h"

// 读取配置文件（返回的 buffer 需由调用者 free）
static bool read_file(const char *path, char **buffer, size_t *size) {
	FILE *fp = fopen(path, "rb");
	if (!fp) {
		return false;
	}

	fseek(fp, 0, SEEK_END);
	long fsize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (fsize <= 0) {
		fclose(fp);
		return false;
	}

	char *data = (char *)malloc((size_t)fsize + 1);
	if (!data) {
		fclose(fp);
		return false;
	}

	size_t read_bytes = fread(data, 1, (size_t)fsize, fp);
	fclose(fp);
	data[read_bytes] = '\0';

	*buffer = data;
	if (size) {
		*size = read_bytes;
	}
	return true;
}

void kalman_config_init(KalmanConfig *cfg) {
	if (!cfg) {
		return;
	}
	memset(cfg, 0, sizeof(*cfg));
	cfg->q_yaw = 0.05f;
	cfg->q_pitch = 0.05f;
	cfg->q_v_yaw = cfg->q_yaw * 10.0f;
	cfg->q_v_pitch = cfg->q_pitch * 10.0f;
	cfg->r_yaw = 0.5f;
	cfg->r_pitch = 0.5f;
	cfg->p0_yaw = 10.0f;
	cfg->p0_v_yaw = 2.0f;
	cfg->p0_pitch = 10.0f;
	cfg->p0_v_pitch = 2.0f;
	cfg->min_confirm_frames = 3;
	cfg->lost_frames = 10;
	cfg->aspect_ratio_thresh = 1.5f;
	cfg->dt_default = 1.0f / 30.0f;
	cfg->dt_min = 1.0f / 200.0f;
	cfg->dt_max = 1.0f / 5.0f;
	cfg->confidence_weight = 0.6f;
}

// JSON 读取工具（float/int）
static bool read_float(cJSON *obj, const char *key, float *dst) {
	cJSON *item = cJSON_GetObjectItem(obj, key);
	if (cJSON_IsNumber(item)) {
		*dst = (float)item->valuedouble;
		return true;
	}
	return false;
}

static bool read_int(cJSON *obj, const char *key, int *dst) {
	cJSON *item = cJSON_GetObjectItem(obj, key);
	if (cJSON_IsNumber(item)) {
		*dst = item->valueint;
		return true;
	}
	return false;
}

bool load_kalman_config(const char *path, KalmanConfig *cfg) {
	if (!cfg || !path) {
		return false;
	}

	char *buffer = NULL;
	if (!read_file(path, &buffer, NULL)) {
		return false;
	}

	cJSON *root = cJSON_Parse(buffer);
	free(buffer);
	if (!root) {
		return false;
	}

	cJSON *tracker = cJSON_GetObjectItem(root, "tracker");
	cJSON *cfg_root = cJSON_IsObject(tracker) ? tracker : root;

	cJSON *process_noise = cJSON_GetObjectItem(cfg_root, "process_noise");
	if (cJSON_IsObject(process_noise)) {
		read_float(process_noise, "yaw", &cfg->q_yaw);
		read_float(process_noise, "pitch", &cfg->q_pitch);
	}
	cfg->q_v_yaw = cfg->q_yaw * 10.0f;
	cfg->q_v_pitch = cfg->q_pitch * 10.0f;

	cJSON *measurement_noise = cJSON_GetObjectItem(cfg_root, "measurement_noise");
	if (cJSON_IsObject(measurement_noise)) {
		read_float(measurement_noise, "yaw", &cfg->r_yaw);
		read_float(measurement_noise, "pitch", &cfg->r_pitch);
	}

	cJSON *initial_covariance = cJSON_GetObjectItem(cfg_root, "initial_covariance");
	if (cJSON_IsObject(initial_covariance)) {
		read_float(initial_covariance, "yaw", &cfg->p0_yaw);
		read_float(initial_covariance, "v_yaw", &cfg->p0_v_yaw);
		read_float(initial_covariance, "pitch", &cfg->p0_pitch);
		read_float(initial_covariance, "v_pitch", &cfg->p0_v_pitch);
	}

	cJSON *state_machine = cJSON_GetObjectItem(cfg_root, "state_machine");
	if (cJSON_IsObject(state_machine)) {
		read_int(state_machine, "min_confirm_frames", &cfg->min_confirm_frames);
		read_int(state_machine, "lost_frames", &cfg->lost_frames);
		read_float(state_machine, "aspect_ratio_thresh", &cfg->aspect_ratio_thresh);
	}

	cJSON *timing = cJSON_GetObjectItem(cfg_root, "timing");
	if (cJSON_IsObject(timing)) {
		read_float(timing, "dt_default", &cfg->dt_default);
		read_float(timing, "dt_min", &cfg->dt_min);
		read_float(timing, "dt_max", &cfg->dt_max);
	}

	cJSON *selection = cJSON_GetObjectItem(cfg_root, "selection");
	if (cJSON_IsObject(selection)) {
		read_float(selection, "confidence_weight", &cfg->confidence_weight);
	}

	if (cfg->min_confirm_frames < 1) cfg->min_confirm_frames = 1;
	if (cfg->lost_frames < 0) cfg->lost_frames = 0;
	if (cfg->aspect_ratio_thresh < 1.0f) cfg->aspect_ratio_thresh = 1.0f;
	if (cfg->dt_min <= 0.0f) cfg->dt_min = cfg->dt_default;
	if (cfg->dt_max < cfg->dt_min) cfg->dt_max = cfg->dt_min;
	if (cfg->confidence_weight < 0.0f) cfg->confidence_weight = 0.0f;
	if (cfg->confidence_weight > 1.0f) cfg->confidence_weight = 1.0f;

	cJSON_Delete(root);
	return true;
}

// 初始化对角阵（协方差）
static void kalman_set_identity(float P[4][4], float v0, float v1, float v2, float v3) {
	memset(P, 0, sizeof(float) * 16);
	P[0][0] = v0;
	P[1][1] = v1;
	P[2][2] = v2;
	P[3][3] = v3;
}

// 初始化状态与协方差
void kalman_init(Kalman *kf, const KalmanConfig *cfg, float yaw, float pitch) {
	if (!kf || !cfg) {
		return;
	}
	kf->x[0] = yaw;
	kf->x[1] = 0.0f;
	kf->x[2] = pitch;
	kf->x[3] = 0.0f;
	kalman_set_identity(kf->P, cfg->p0_yaw, cfg->p0_v_yaw, cfg->p0_pitch, cfg->p0_v_pitch);
	kf->initialized = true;
}

// 预测步骤：根据 dt 外推状态
void kalman_predict(Kalman *kf, const KalmanConfig *cfg, float dt) {
	if (!kf || !cfg || !kf->initialized) {
		return;
	}

	// State prediction: x = A x
	// A = [[1, dt, 0, 0], [0,1,0,0], [0,0,1,dt], [0,0,0,1]]
	kf->x[0] = kf->x[0] + dt * kf->x[1];
	kf->x[2] = kf->x[2] + dt * kf->x[3];

	// Covariance prediction: P = A P A^T + Q
	float A[4][4] = {
			{1.0f, dt, 0.0f, 0.0f},
			{0.0f, 1.0f, 0.0f, 0.0f},
			{0.0f, 0.0f, 1.0f, dt},
			{0.0f, 0.0f, 0.0f, 1.0f},
	};
	float AP[4][4] = {0};
	float Pn[4][4] = {0};

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			for (int k = 0; k < 4; k++) {
				AP[i][j] += A[i][k] * kf->P[k][j];
			}
		}
	}

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			for (int k = 0; k < 4; k++) {
				Pn[i][j] += AP[i][k] * A[j][k];
			}
		}
	}

	// Process noise (constant-velocity model with acceleration noise)
	float dt2 = dt * dt;
	float dt3 = dt2 * dt;
	float dt4 = dt3 * dt;
	float var_yaw = cfg->q_yaw * cfg->q_yaw;
	float var_pitch = cfg->q_pitch * cfg->q_pitch;

	// Yaw block: [yaw, v_yaw]
	Pn[0][0] += 0.25f * dt4 * var_yaw;
	Pn[0][1] += 0.50f * dt3 * var_yaw;
	Pn[1][0] += 0.50f * dt3 * var_yaw;
	Pn[1][1] += dt2 * var_yaw;

	// Pitch block: [pitch, v_pitch]
	Pn[2][2] += 0.25f * dt4 * var_pitch;
	Pn[2][3] += 0.50f * dt3 * var_pitch;
	Pn[3][2] += 0.50f * dt3 * var_pitch;
	Pn[3][3] += dt2 * var_pitch;

	memcpy(kf->P, Pn, sizeof(Pn));
}

// 更新步骤：融合观测（yaw/pitch）
void kalman_update(Kalman *kf, const KalmanConfig *cfg, float yaw, float pitch) {
	if (!kf || !cfg || !kf->initialized) {
		return;
	}

	// Measurement residual: y = z - Hx
	float y0 = yaw - kf->x[0];
	float y1 = pitch - kf->x[2];

	// S = H P H^T + R
	float S00 = kf->P[0][0] + cfg->r_yaw;
	float S01 = kf->P[0][2];
	float S10 = kf->P[2][0];
	float S11 = kf->P[2][2] + cfg->r_pitch;

	float det = (S00 * S11) - (S01 * S10);
	if (fabsf(det) < 1e-6f) {
		return;
	}

	float invS00 = S11 / det;
	float invS01 = -S01 / det;
	float invS10 = -S10 / det;
	float invS11 = S00 / det;

	// K = P H^T S^-1
	float K00 = (kf->P[0][0] * invS00) + (kf->P[0][2] * invS10);
	float K01 = (kf->P[0][0] * invS01) + (kf->P[0][2] * invS11);
	float K10 = (kf->P[1][0] * invS00) + (kf->P[1][2] * invS10);
	float K11 = (kf->P[1][0] * invS01) + (kf->P[1][2] * invS11);
	float K20 = (kf->P[2][0] * invS00) + (kf->P[2][2] * invS10);
	float K21 = (kf->P[2][0] * invS01) + (kf->P[2][2] * invS11);
	float K30 = (kf->P[3][0] * invS00) + (kf->P[3][2] * invS10);
	float K31 = (kf->P[3][0] * invS01) + (kf->P[3][2] * invS11);

	// x = x + K y
	kf->x[0] += (K00 * y0) + (K01 * y1);
	kf->x[1] += (K10 * y0) + (K11 * y1);
	kf->x[2] += (K20 * y0) + (K21 * y1);
	kf->x[3] += (K30 * y0) + (K31 * y1);

	// P = (I - K H) P
	float P00 = kf->P[0][0];
	float P01 = kf->P[0][1];
	float P02 = kf->P[0][2];
	float P03 = kf->P[0][3];
	float P10 = kf->P[1][0];
	float P11 = kf->P[1][1];
	float P12 = kf->P[1][2];
	float P13 = kf->P[1][3];
	float P20 = kf->P[2][0];
	float P21 = kf->P[2][1];
	float P22 = kf->P[2][2];
	float P23 = kf->P[2][3];
	float P30 = kf->P[3][0];
	float P31 = kf->P[3][1];
	float P32 = kf->P[3][2];
	float P33 = kf->P[3][3];

	kf->P[0][0] = P00 - (K00 * P00 + K01 * P20);
	kf->P[0][1] = P01 - (K00 * P01 + K01 * P21);
	kf->P[0][2] = P02 - (K00 * P02 + K01 * P22);
	kf->P[0][3] = P03 - (K00 * P03 + K01 * P23);

	kf->P[1][0] = P10 - (K10 * P00 + K11 * P20);
	kf->P[1][1] = P11 - (K10 * P01 + K11 * P21);
	kf->P[1][2] = P12 - (K10 * P02 + K11 * P22);
	kf->P[1][3] = P13 - (K10 * P03 + K11 * P23);

	kf->P[2][0] = P20 - (K20 * P00 + K21 * P20);
	kf->P[2][1] = P21 - (K20 * P01 + K21 * P21);
	kf->P[2][2] = P22 - (K20 * P02 + K21 * P22);
	kf->P[2][3] = P23 - (K20 * P03 + K21 * P23);

	kf->P[3][0] = P30 - (K30 * P00 + K31 * P20);
	kf->P[3][1] = P31 - (K30 * P01 + K31 * P21);
	kf->P[3][2] = P32 - (K30 * P02 + K31 * P22);
	kf->P[3][3] = P33 - (K30 * P03 + K31 * P23);
}
