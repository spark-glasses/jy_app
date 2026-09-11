#pragma once

#include "spark.h"

typedef struct spark_assistant_avatar spark_assistant_avatar_t;

/** Create one precomputed dithered sphere. The parent owns its lifetime. */
spark_assistant_avatar_t* spark_assistant_avatar_create(lv_obj_t* parent, int32_t size);

/** Return the LVGL root used for layout. */
lv_obj_t* spark_assistant_avatar_object(spark_assistant_avatar_t* avatar);

/** Roll the existing sphere in from the left. */
void spark_assistant_avatar_roll_in(spark_assistant_avatar_t* avatar);

/** Apply one validated assistant state to the existing avatar. */
bool spark_assistant_avatar_set_state(
    spark_assistant_avatar_t* avatar, const spark_assistant_presentation_t* presentation);
