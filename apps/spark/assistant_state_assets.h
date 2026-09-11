#pragma once

#include "lvgl/lvgl.h"

#define SPARK_AVATAR_ASSET_SIZE 40
#define SPARK_AVATAR_ASSET_STRIDE 5
#define SPARK_AVATAR_ASSET_PALETTE_BYTES 8
#define SPARK_AVATAR_ASSET_FRAME_BYTES \
    (SPARK_AVATAR_ASSET_PALETTE_BYTES + SPARK_AVATAR_ASSET_STRIDE * SPARK_AVATAR_ASSET_SIZE)

typedef struct {
    const lv_image_dsc_t* frames;
    uint8_t frame_count;
    uint16_t frame_period_ms;
} spark_avatar_frame_sequence_t;
