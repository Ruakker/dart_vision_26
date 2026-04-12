// 串口通信：基于 drv_cmd_com 发送
#include "../include/dart_vision/uart.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "../include/dart_vision/visual_data.h"
#include "tool/drv_cmd_com.h"

State *uart_g_state = NULL;

FeedbackPacket_t g_feedback_payload;
DetectionPacket_t g_detection_payload;
uint8_t tx_buffer[8192];
uint8_t rx_buffer[8192];
unsigned int rx_buffer_len;

int uart_init(const char *device, int baud_rate) {
  int fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
  if (fd == -1) {
    perror("open_port: Unable to open UART device");
    return -1;
  }

  struct termios options;
  tcgetattr(fd, &options);

  speed_t baud;
  switch (baud_rate) {
    case 4800:
      baud = B4800;
      break;
    case 9600:
      baud = B9600;
      break;
    case 19200:
      baud = B19200;
      break;
    case 38400:
      baud = B38400;
      break;
    case 57600:
      baud = B57600;
      break;
    case 115200:
      baud = B115200;
      break;
    case 230400:
      baud = B230400;
      break;
    case 460800:
      baud = B460800;
      break;

    default:
      printf("Warning: Baud rate %d not supported, defaulting to 115200\n", baud_rate);
      baud = B115200;
      break;
  }
  cfsetispeed(&options, baud);
  cfsetospeed(&options, baud);

  // 8N1
  options.c_cflag &= ~PARENB;
  options.c_cflag &= ~CSTOPB;
  options.c_cflag &= ~CSIZE;
  options.c_cflag |= CS8;

  options.c_cflag |= (CLOCAL | CREAD);

  // Raw 模式
  options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
  options.c_oflag &= ~OPOST;
  options.c_iflag &= ~(IXON | IXOFF | IXANY);

  // 阻塞读取
  options.c_cc[VMIN] = 0;
  options.c_cc[VTIME] = 10;

  tcsetattr(fd, TCSANOW, &options);
  fcntl(fd, F_SETFL, 0);

  printf("UART initialized: %s @ %d\n", device, baud_rate);
  return fd;
}

void send_detection(int fd, uint8_t detected, float yaw_deg, float pitch_deg, float v_yaw_deg,
                    float v_pitch_deg, float distance_m) {
  if (fd < 0) {
    return;
  }

  g_detection_payload.detected = detected;
  g_detection_payload.yaw = yaw_deg;
  g_detection_payload.pitch = pitch_deg;
  g_detection_payload.v_yaw = v_yaw_deg;
  g_detection_payload.v_pitch = v_pitch_deg;
  g_detection_payload.distance = distance_m;
  // printf("Send yaw: %.2f\n", yaw_deg);

  Send_Cmd_Packet((void *)(intptr_t)fd, tx_buffer, sizeof(tx_buffer), 0);
}

static int logger_cnt = 0;
void feedback_callback(void *data) {
  const FeedbackPacket_t *pkt = (const FeedbackPacket_t *)data;

  // printf("feedback: %f %f %f %f\n", pkt->servo_angle_trans.angle_lu, pkt->servo_angle_trans.angle_ld,
  //        pkt->servo_angle_trans.angle_ru, pkt->servo_angle_trans.angle_rd);

  // 获取当前时间
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  struct tm tm_info;
  localtime_r(&ts.tv_sec, &tm_info);

  char time_str[20];
  snprintf(time_str, sizeof(time_str), "%02d:%02d.%03ld", tm_info.tm_min, tm_info.tm_sec,
           ts.tv_nsec / 1000000);

  // 写入日志文件
  if (uart_g_state && uart_g_state->log_fp) {
    // 将所有的收到的内容写入日志文件中
    fprintf(uart_g_state->log_fp, "[%s] u: %.2f, v: %.2f, w: %.2f, Pitch: %.2f, Yaw: %.2f\n",
            time_str,
            pkt->u, pkt->v, pkt->w, pkt->Pitch, pkt->Yaw);
    logger_cnt++;
    if (logger_cnt >= 100) {
      fflush(uart_g_state->log_fp);  // 定期刷新，防止数据丢失
      logger_cnt = 0;
    }
  }

  // printf("Received feedback data: %f\n", pkt->data);
}

void *run_uart_recv(void *args) {
  UartThreadArgs *thread_args = (UartThreadArgs *)args;
  int fd = thread_args->fd;
  State *state = thread_args->state;

  while (!state_should_exit(state)) {
    unsigned char rx[1024];
    ssize_t n = read(fd, rx, sizeof(rx));
    if (n > 0) {
      if (rx_buffer_len + (unsigned int)n > sizeof(rx_buffer)) {
        rx_buffer_len = 0;
        printf("Warning: RX buffer overflow, clearing buffer\n");
      }
      memcpy(rx_buffer + rx_buffer_len, rx, (size_t)n);
      rx_buffer_len += (unsigned int)n;

      int consumed = Receive_Handle_Consumed(rx_buffer, (unsigned short)rx_buffer_len);
      if (consumed > 0) {
        if (consumed < rx_buffer_len) {
          // 还没处理完的数据（半包），移到最前面
          memmove(rx_buffer, rx_buffer + consumed, rx_buffer_len - consumed);
          rx_buffer_len -= consumed;
        } else {
          // 全部处理完了
          rx_buffer_len = 0;
        }
      }
    } else if (n < 0) {
      if (errno == EINTR) {  // ctrl+c
        continue;
      }

      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        usleep(100);
        continue;
      }

      perror("UART Read Fatal Error");
      break;
    }
  }

  printf("Exit UART recv thread\n");
  pthread_exit(NULL);
}

void *run_uart_send(void *args) {
  UartThreadArgs *thread_args = (UartThreadArgs *)args;
  int fd = thread_args->fd;
  State *state = thread_args->state;

  uint64_t last_sent_seq = 0;

  while (!state_should_exit(state)) {
    pthread_mutex_lock(&state->track_mutex);
    while (!state_should_exit(state) &&
           (!state->track_ts_valid || state->track_seq == last_sent_seq)) {
      pthread_cond_wait(&state->track_cv, &state->track_mutex);
    }
    if (state_should_exit(state)) {
      pthread_mutex_unlock(&state->track_mutex);
      break;
    }
    last_sent_seq = state->track_seq;
    pthread_mutex_unlock(&state->track_mutex);

    TrackOutput track = {0};
    state_copy_track(state, &track);

    if (track.detected) {
      float distance_m = track.distance_valid ? track.distance : 0.0f;
      send_detection(fd, true, track.yaw, track.pitch, track.v_yaw, track.v_pitch, distance_m);
    } else {
      send_detection(fd, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    }
    // 打印发送频率
    // static struct timespec time_stamp, last_time_stamp;
    // static double average_fps = 0.0;
    // clock_gettime(CLOCK_MONOTONIC, &time_stamp);
    // double time_used = (time_stamp.tv_sec - last_time_stamp.tv_sec) +
    //                     (time_stamp.tv_nsec - last_time_stamp.tv_nsec) / 1000000000.0;
  
    // average_fps = (average_fps * 9 + 1.0 / time_used) / 10;
    // printf("UART Send Time interval: %f seconds, FPS: %f, Average FPS: %f\n", time_used, 1.0 / time_used,
    //         average_fps);
    // last_time_stamp = time_stamp;
  }

  printf("Exit UART send thread\n");
  pthread_exit(NULL);
}