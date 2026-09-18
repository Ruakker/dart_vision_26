#include "dart_vision/record_writer.h"

#include <sample_comm.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct RecordFrame {
  struct RecordFrame *next;
  size_t bytes;
  unsigned int pack_count;
  bool is_idr;
  size_t lengths[];
};

static uint8_t *record_frame_data(RecordFrame *frame) {
  return (uint8_t *)(frame->lengths + frame->pack_count);
}

static bool stream_is_idr(const VENC_STREAM_S *stream) {
  for (unsigned int i = 0; i < stream->u32PackCount; ++i) {
    if (stream->pstPack[i].DataType.enH264EType == H264E_NALU_IDRSLICE) {
      return true;
    }
  }
  return false;
}

static void record_frame_destroy(RecordFrame *frame) {
  free(frame);
}

static RecordFrame *record_frame_copy(const VENC_STREAM_S *stream) {
  if (!stream || !stream->pstPack || stream->u32PackCount == 0) {
    return NULL;
  }

  size_t bytes = 0;
  for (unsigned int i = 0; i < stream->u32PackCount; ++i) {
    const VENC_PACK_S *pack = &stream->pstPack[i];
    if (pack->u32Offset > pack->u32Len || SIZE_MAX - bytes < pack->u32Len - pack->u32Offset) {
      return NULL;
    }
    bytes += pack->u32Len - pack->u32Offset;
  }

  if (bytes == 0 || stream->u32PackCount > (SIZE_MAX - sizeof(RecordFrame)) / sizeof(size_t) ||
      bytes > SIZE_MAX - sizeof(RecordFrame) - stream->u32PackCount * sizeof(size_t)) {
    return NULL;
  }

  RecordFrame *frame = malloc(sizeof(*frame) + stream->u32PackCount * sizeof(*frame->lengths) + bytes);
  if (!frame) {
    return NULL;
  }

  frame->next = NULL;
  frame->bytes = bytes;
  frame->pack_count = stream->u32PackCount;
  frame->is_idr = stream_is_idr(stream);

  uint8_t *dst = record_frame_data(frame);
  for (unsigned int i = 0; i < stream->u32PackCount; ++i) {
    const VENC_PACK_S *pack = &stream->pstPack[i];
    size_t length = pack->u32Len - pack->u32Offset;
    frame->lengths[i] = length;
    memcpy(dst, pack->pu8Addr + pack->u32Offset, length);
    dst += length;
  }

  return frame;
}

static void record_writer_log_sync_error(const char *name) {
  printf("Record writer %s failed: %s\n", name, strerror(errno));
}

static void record_writer_write_frame(RecordWriter *writer, RecordFrame *frame,
                                      unsigned int *frames_since_sync) {
  uint8_t *data = record_frame_data(frame);
  bool write_ok = true;
  for (unsigned int i = 0; i < frame->pack_count; ++i) {
    if (fwrite(data, 1, frame->lengths[i], writer->fp) != frame->lengths[i]) {
      printf("Record writer fwrite failed: %s\n", strerror(errno));
      pthread_mutex_lock(&writer->mutex);
      writer->stats.write_errors++;
      writer->accepting = false;
      writer->waiting_for_idr = true;
      pthread_mutex_unlock(&writer->mutex);
      write_ok = false;
      break;
    }
    data += frame->lengths[i];
  }

  if (write_ok) {
    pthread_mutex_lock(&writer->mutex);
    writer->stats.written_frames++;
    writer->stats.written_bytes += frame->bytes;
    pthread_mutex_unlock(&writer->mutex);
    (*frames_since_sync)++;
  }

  if (*frames_since_sync >= writer->sync_interval_frames) {
    struct timespec start;
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    if (fflush(writer->fp) != 0) {
      record_writer_log_sync_error("fflush");
      pthread_mutex_lock(&writer->mutex);
      writer->stats.flush_errors++;
      pthread_mutex_unlock(&writer->mutex);
    }
    if (fdatasync(fileno(writer->fp)) != 0) {
      record_writer_log_sync_error("fdatasync");
      pthread_mutex_lock(&writer->mutex);
      writer->stats.sync_errors++;
      pthread_mutex_unlock(&writer->mutex);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    long long elapsed_us = (end.tv_sec - start.tv_sec) * 1000000LL +
                           (end.tv_nsec - start.tv_nsec) / 1000LL;
    printf("Record writer sync: %lld us\n", elapsed_us);
    *frames_since_sync = 0;
  }
}

static void *record_writer_run(void *arg) {
  RecordWriter *writer = arg;
  unsigned int frames_since_sync = 0;

  while (true) {
    pthread_mutex_lock(&writer->mutex);
    while (!writer->head && !writer->stop_requested) {
      pthread_cond_wait(&writer->not_empty, &writer->mutex);
    }
    if (!writer->head && writer->stop_requested) {
      pthread_mutex_unlock(&writer->mutex);
      break;
    }

    RecordFrame *frame = writer->head;
    writer->head = frame->next;
    if (!writer->head) {
      writer->tail = NULL;
    }
    writer->queued_bytes -= frame->bytes;
    writer->queued_frames--;
    pthread_mutex_unlock(&writer->mutex);

    record_writer_write_frame(writer, frame, &frames_since_sync);
    record_frame_destroy(frame);
  }

  if (fflush(writer->fp) != 0) {
    record_writer_log_sync_error("final fflush");
    pthread_mutex_lock(&writer->mutex);
    writer->stats.flush_errors++;
    pthread_mutex_unlock(&writer->mutex);
  }
  if (fdatasync(fileno(writer->fp)) != 0) {
    record_writer_log_sync_error("final fdatasync");
    pthread_mutex_lock(&writer->mutex);
    writer->stats.sync_errors++;
    pthread_mutex_unlock(&writer->mutex);
  }

  return NULL;
}

void record_writer_init(RecordWriter *writer, FILE *fp, const RecordWriterConfig *config) {
  memset(writer, 0, sizeof(*writer));
  writer->fp = fp;
  writer->max_bytes = config->max_bytes;
  writer->max_frames = config->max_frames;
  writer->sync_interval_frames = config->sync_interval_frames;
  writer->accepting = true;
  pthread_mutex_init(&writer->mutex, NULL);
  pthread_cond_init(&writer->not_empty, NULL);
}

bool record_writer_start(RecordWriter *writer) {
  if (!writer || !writer->fp || writer->started) {
    return false;
  }
  if (pthread_create(&writer->thread, NULL, record_writer_run, writer) != 0) {
    return false;
  }
  writer->started = true;
  return true;
}

bool record_writer_submit(RecordWriter *writer, const VENC_STREAM_S *stream) {
  if (!writer || !writer->started || !stream) {
    return false;
  }

  RecordFrame *frame = record_frame_copy(stream);
  if (!frame) {
    pthread_mutex_lock(&writer->mutex);
    writer->stats.allocation_errors++;
    writer->stats.dropped_frames++;
    writer->waiting_for_idr = true;
    pthread_mutex_unlock(&writer->mutex);
    return false;
  }

  pthread_mutex_lock(&writer->mutex);
  if (!writer->accepting || writer->stop_requested) {
    writer->stats.dropped_frames++;
    pthread_mutex_unlock(&writer->mutex);
    record_frame_destroy(frame);
    return false;
  }
  if (writer->waiting_for_idr && !frame->is_idr) {
    writer->stats.dropped_frames++;
    writer->stats.dropped_waiting_idr++;
    pthread_mutex_unlock(&writer->mutex);
    record_frame_destroy(frame);
    return false;
  }
  if (frame->bytes > writer->max_bytes || writer->queued_frames >= writer->max_frames ||
      writer->queued_bytes > writer->max_bytes - frame->bytes) {
    writer->stats.dropped_frames++;
    writer->waiting_for_idr = true;
    pthread_mutex_unlock(&writer->mutex);
    record_frame_destroy(frame);
    return false;
  }
  if (writer->waiting_for_idr) {
    writer->waiting_for_idr = false;
    writer->stats.idr_recoveries++;
  }

  if (writer->tail) {
    writer->tail->next = frame;
  } else {
    writer->head = frame;
  }
  writer->tail = frame;
  writer->queued_bytes += frame->bytes;
  writer->queued_frames++;
  writer->stats.submitted_frames++;
  if (writer->queued_bytes > writer->stats.peak_queued_bytes) {
    writer->stats.peak_queued_bytes = writer->queued_bytes;
  }
  if (writer->queued_frames > writer->stats.peak_queued_frames) {
    writer->stats.peak_queued_frames = writer->queued_frames;
  }
  pthread_cond_signal(&writer->not_empty);
  pthread_mutex_unlock(&writer->mutex);
  return true;
}

void record_writer_request_stop(RecordWriter *writer) {
  if (!writer || !writer->started) {
    return;
  }
  pthread_mutex_lock(&writer->mutex);
  writer->accepting = false;
  writer->stop_requested = true;
  pthread_cond_broadcast(&writer->not_empty);
  pthread_mutex_unlock(&writer->mutex);
}

void record_writer_join(RecordWriter *writer) {
  if (!writer || !writer->started) {
    return;
  }
  pthread_join(writer->thread, NULL);
  writer->started = false;
}

void record_writer_destroy(RecordWriter *writer) {
  if (!writer) {
    return;
  }
  RecordFrame *frame = writer->head;
  while (frame) {
    RecordFrame *next = frame->next;
    record_frame_destroy(frame);
    frame = next;
  }
  writer->head = NULL;
  writer->tail = NULL;
  pthread_cond_destroy(&writer->not_empty);
  pthread_mutex_destroy(&writer->mutex);
}

void record_writer_get_stats(RecordWriter *writer, RecordWriterStats *stats) {
  if (!writer || !stats) {
    return;
  }
  pthread_mutex_lock(&writer->mutex);
  *stats = writer->stats;
  pthread_mutex_unlock(&writer->mutex);
}
