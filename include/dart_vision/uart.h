#ifndef DART_VISION_UART_H_
#define DART_VISION_UART_H_

#include <dart_vision/state.h>
#include <dart_vision/visual_data.h>
#include <stdint.h>

extern FeedbackPacket_t g_feedback_payload;

extern DetectionPacket_t g_detection_payload;
extern uint8_t tx_buffer[8192];
extern uint8_t rx_buffer[8192];
extern unsigned int rx_buffer_len;

typedef struct {
  int fd;
  State *state;
} UartThreadArgs;

extern State *uart_g_state;

int uart_init(const char *device, int baud_rate);
void *run_uart_recv(void *args);
void *run_uart_send(void *args);
void send_detection(int fd, uint8_t detected, float yaw_deg, float pitch_deg, float v_yaw_deg,
                    float v_pitch_deg, float distance_m);
void feedback_callback(void *data);

#endif
