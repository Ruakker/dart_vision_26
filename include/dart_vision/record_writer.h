#ifndef DART_VISION_RECORD_WRITER_H_
#define DART_VISION_RECORD_WRITER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>

typedef struct _VENC_STREAM_S VENC_STREAM_S;

typedef struct RecordFrame RecordFrame;

typedef struct {
  size_t max_bytes;
  unsigned int max_frames;
  unsigned int sync_interval_frames;
} RecordWriterConfig;

typedef struct {
  uint64_t submitted_frames;
  uint64_t written_frames;
  uint64_t written_bytes;
  uint64_t dropped_frames;
  uint64_t dropped_waiting_idr;
  uint64_t allocation_errors;
  uint64_t write_errors;
  uint64_t flush_errors;
  uint64_t sync_errors;
  uint64_t idr_recoveries;
  size_t peak_queued_bytes;
  unsigned int peak_queued_frames;
} RecordWriterStats;

typedef struct {
  FILE *fp;
  pthread_t thread;
  pthread_mutex_t mutex;
  pthread_cond_t not_empty;
  RecordFrame *head;
  RecordFrame *tail;
  size_t queued_bytes;
  unsigned int queued_frames;
  size_t max_bytes;
  unsigned int max_frames;
  unsigned int sync_interval_frames;
  bool accepting;
  bool stop_requested;
  bool started;
  bool waiting_for_idr;
  RecordWriterStats stats;
} RecordWriter;

void record_writer_init(RecordWriter *writer, FILE *fp, const RecordWriterConfig *config);
bool record_writer_start(RecordWriter *writer);
bool record_writer_submit(RecordWriter *writer, const VENC_STREAM_S *stream);
void record_writer_request_stop(RecordWriter *writer);
void record_writer_join(RecordWriter *writer);
void record_writer_destroy(RecordWriter *writer);
void record_writer_get_stats(RecordWriter *writer, RecordWriterStats *stats);

#endif
