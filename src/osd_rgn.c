#include "osd_rgn.h"

#include <stdio.h>
#include <string.h>

#include "cvi_sys.h"
#include "osd_font8x12.h"

static uint16_t argb1555(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(0x8000u | ((uint16_t)(r >> 3) << 10) |
                    ((uint16_t)(g >> 3) << 5) | (uint16_t)(b >> 3));
}

static void set_pixel(uint16_t *canvas, uint32_t stride_pixels, int x, int y,
                      uint16_t color) {
  if (x >= 0 && x < OSD_RGN_LOGICAL_WIDTH && y >= 0 && y < OSD_RGN_LOGICAL_HEIGHT) {
    // 输出画面需要逆时针旋转 90 度：逻辑 (x,y) -> 物理 (y,H-1-x)。
    int physical_x = y;
    int physical_y = OSD_RGN_LOGICAL_WIDTH - 1 - x;
    canvas[(size_t)physical_y * stride_pixels + (size_t)physical_x] = color;
  }
}

static void draw_line(uint16_t *canvas, uint32_t stride_pixels, int line,
                      const char *text, uint16_t color) {
  if (!text || line < 0 || line >= OSD_RGN_LINES) {
    return;
  }

  int column = 0;
  int y0 = line * OSD_RGN_LINE_HEIGHT;
  for (const unsigned char *p = (const unsigned char *)text;
       *p && *p != '\n' && column < OSD_RGN_WIDTH / OSD_FONT_W; ++p, ++column) {
    if (*p < OSD_FONT_ASCII_START ||
        *p >= OSD_FONT_ASCII_START + OSD_FONT_ASCII_COUNT) {
      continue;
    }
    const uint8_t *glyph = g_osd_font8x12[*p - OSD_FONT_ASCII_START];
    for (int row = 0; row < OSD_FONT_H; ++row) {
      for (int bit = 0; bit < OSD_FONT_W; ++bit) {
        if (glyph[row] & (uint8_t)(1u << (7 - bit))) {
          set_pixel(canvas, stride_pixels, column * OSD_FONT_W + bit,
                    y0 + row, color);
        }
      }
    }
  }
}

CVI_S32 osd_rgn_init(OsdRgnContext *ctx) {
  if (!ctx) {
    return CVI_FAILURE;
  }
  memset(ctx, 0, sizeof(*ctx));

  RGN_ATTR_S attr = {0};
  attr.enType = OVERLAY_RGN;
  attr.unAttr.stOverlay.enPixelFormat = PIXEL_FORMAT_ARGB_1555;
  attr.unAttr.stOverlay.u32BgColor = 0x0000;
  attr.unAttr.stOverlay.stSize.u32Width = OSD_RGN_WIDTH;
  attr.unAttr.stOverlay.stSize.u32Height = OSD_RGN_HEIGHT;
  attr.unAttr.stOverlay.u32CanvasNum = 2;
  attr.unAttr.stOverlay.stCompressInfo.enOSDCompressMode = OSD_COMPRESS_MODE_NONE;

  CVI_S32 ret = CVI_RGN_Create(OSD_RGN_HANDLE, &attr);
  if (ret != CVI_SUCCESS) {
    printf("CVI_RGN_Create failed: %#x\n", ret);
    return ret;
  }

  MMF_CHN_S chn = {0};
  chn.enModId = CVI_ID_VPSS;
  chn.s32DevId = 0;
  chn.s32ChnId = 0;

  RGN_CHN_ATTR_S chn_attr = {0};
  chn_attr.bShow = CVI_TRUE;
  chn_attr.enType = OVERLAY_RGN;
  chn_attr.unChnAttr.stOverlayChn.stPoint.s32X = 0;
  chn_attr.unChnAttr.stOverlayChn.stPoint.s32Y = 0;
  chn_attr.unChnAttr.stOverlayChn.u32Layer = 0;

  ret = CVI_RGN_AttachToChn(OSD_RGN_HANDLE, &chn, &chn_attr);
  if (ret != CVI_SUCCESS) {
    printf("CVI_RGN_AttachToChn failed: %#x\n", ret);
    CVI_RGN_Destroy(OSD_RGN_HANDLE);
    return ret;
  }

  ctx->handle = OSD_RGN_HANDLE;
  ctx->initialized = true;
  return CVI_SUCCESS;
}

void osd_rgn_deinit(OsdRgnContext *ctx) {
  if (!ctx || !ctx->initialized) {
    return;
  }

  MMF_CHN_S chn = {0};
  chn.enModId = CVI_ID_VPSS;
  chn.s32DevId = 0;
  chn.s32ChnId = 0;
  CVI_RGN_DetachFromChn(ctx->handle, &chn);
  CVI_RGN_Destroy(ctx->handle);
  ctx->initialized = false;
}

CVI_S32 osd_rgn_update(OsdRgnContext *ctx, const char *lines[OSD_RGN_LINES]) {
  if (!ctx || !ctx->initialized || !lines) {
    return CVI_FAILURE;
  }

  RGN_CANVAS_INFO_S canvas_info = {0};
  CVI_S32 ret = CVI_RGN_GetCanvasInfo(ctx->handle, &canvas_info);
  if (ret != CVI_SUCCESS) {
    return ret;
  }

  uint32_t map_size = canvas_info.u32Stride * canvas_info.stSize.u32Height;
  uint16_t *canvas = (uint16_t *)CVI_SYS_Mmap(canvas_info.u64PhyAddr, map_size);
  if (!canvas) {
    return CVI_FAILURE;
  }

  uint32_t stride_pixels = canvas_info.u32Stride / sizeof(uint16_t);
  uint16_t bg = 0x0000;
  uint16_t light_blue = argb1555(96, 208, 255);
  uint16_t white = argb1555(255, 255, 255);
  uint16_t light_yellow = argb1555(255, 255, 176);

  for (uint32_t y = 0; y < canvas_info.stSize.u32Height; ++y) {
    for (uint32_t x = 0; x < canvas_info.stSize.u32Width; ++x) {
      canvas[(size_t)y * stride_pixels + x] = bg;
    }
  }

  for (int line = 0; line < 3; ++line) {
    draw_line(canvas, stride_pixels, line, lines[line], light_blue);
  }
  for (int line = 3; line < OSD_RGN_LINES - 1; ++line) {
    draw_line(canvas, stride_pixels, line, lines[line], white);
  }
  draw_line(canvas, stride_pixels, OSD_RGN_LINES - 1,
            lines[OSD_RGN_LINES - 1], light_yellow);

  ret = CVI_RGN_UpdateCanvas(ctx->handle);
  CVI_SYS_Munmap(canvas, map_size);
  return ret;
}
