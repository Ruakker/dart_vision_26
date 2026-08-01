#ifndef DART_VISION_OSD_RGN_H_
#define DART_VISION_OSD_RGN_H_

#include <stdbool.h>
#include <stdint.h>

#include "cvi_region.h"

#define OSD_RGN_HANDLE 0
#define OSD_RGN_LOGICAL_WIDTH 320
#define OSD_RGN_LINES 8
#define OSD_RGN_LINE_HEIGHT 13
#define OSD_RGN_LOGICAL_HEIGHT (OSD_RGN_LINES * OSD_RGN_LINE_HEIGHT)
#define OSD_RGN_WIDTH OSD_RGN_LOGICAL_HEIGHT
#define OSD_RGN_HEIGHT OSD_RGN_LOGICAL_WIDTH
#define OSD_RGN_TEXT_MAX 96

typedef struct {
  RGN_HANDLE handle;
  bool initialized;
} OsdRgnContext;

CVI_S32 osd_rgn_init(OsdRgnContext *ctx);
void osd_rgn_deinit(OsdRgnContext *ctx);
CVI_S32 osd_rgn_update(OsdRgnContext *ctx, const char *lines[OSD_RGN_LINES]);

#endif
